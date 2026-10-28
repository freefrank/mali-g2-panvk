/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2026 NXP
 * Copyright (C) 2021 GlobalLogic Ukraine
 * Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)
 * SPDX-License-Identifier: MIT
 */

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/ChromaSiting.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidl/android/hardware/graphics/common/ExtendableType.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponent.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponentType.h>
#include <android/hardware/graphics/mapper/IMapper.h>
#include <gralloctypes/Gralloc4.h>
#include <system/window.h>

#include <dlfcn.h>
#include <glob.h>
#include <string.h>
#include <unistd.h>

#include "util/log.h"
#include "u_gralloc_internal.h"

using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::ChromaSiting;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::PlaneLayoutComponent;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;

using MetadataType =
   android::hardware::graphics::mapper::V4_0::IMapper::MetadataType;

struct gralloc4 {
   struct u_gralloc base;
   android::sp<IMapper> mapper;
   AIMapper *mapper5 = nullptr;
   void *mapper5_library = nullptr;
   /* XCLIPSE: handles the mapper cannot describe go to the handle parsers instead. */
   struct u_gralloc *fallback;
};

static AIMapper *
LoadMapper5(void **library)
{
   void *support = dlopen("libvndksupport.so", RTLD_NOW | RTLD_LOCAL);
   auto load_sphal = support ? reinterpret_cast<void *(*)(const char *, int)>(
      dlsym(support, "android_load_sphal_library")) : nullptr;
   const char *patterns[] = {
#ifdef __LP64__
      "/vendor/lib64/hw/mapper.*.so", "/odm/lib64/hw/mapper.*.so",
#else
      "/vendor/lib/hw/mapper.*.so", "/odm/lib/hw/mapper.*.so",
#endif
   };
   AIMapper *mapper = nullptr;
   for (const char *pattern : patterns) {
      glob_t paths = {};
      if (glob(pattern, 0, nullptr, &paths) != 0) {
         globfree(&paths);
         continue;
      }
      for (size_t i = 0; i < paths.gl_pathc && !mapper; i++) {
         const char *path = paths.gl_pathv[i];
         void *module = load_sphal ? load_sphal(path, RTLD_NOW | RTLD_LOCAL) : nullptr;
         if (!module)
            module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
         if (!module)
            continue;
         auto load = reinterpret_cast<decltype(&AIMapper_loadIMapper)>(
            dlsym(module, "AIMapper_loadIMapper"));
         AIMapper *candidate = nullptr;
         if (load && load(&candidate) == AIMAPPER_ERROR_NONE && candidate &&
             candidate->version >= AIMAPPER_VERSION_5) {
            mapper = candidate;
            *library = module;
         } else {
            dlclose(module);
         }
      }
      globfree(&paths);
      if (mapper)
         break;
   }
   if (support)
      dlclose(support);
   return mapper;
}

static Error
GetMetadata(const gralloc4 *gr, const native_handle_t *buffer,
            MetadataType type, hidl_vec<uint8_t> *metadata)
{
   if (gr->mapper5) {
      AIMapper_MetadataType token = {type.name.c_str(), type.value};
      auto get = gr->mapper5->v5.getMetadata;
      int32_t size = get(buffer, token, nullptr, 0);
      if (size <= 0 || size > 1048576)
         return Error::BAD_VALUE;
      metadata->resize(size);
      if (get(buffer, token, metadata->data(), metadata->size()) != size)
         return Error::BAD_VALUE;
      return Error::NONE;
   }

   Error error = Error::NONE;

   auto native_handle = const_cast<native_handle_t *>(buffer);

   auto ret =
      gr->mapper->get(native_handle, type,
                  [&](const auto &get_error, const auto &get_metadata) {
                     error = get_error;
                     *metadata = get_metadata;
                  });

   if (!ret.isOk())
      error = Error::NO_RESOURCES;

   return error;
}

static std::optional<std::vector<PlaneLayout>>
GetPlaneLayouts(const gralloc4 *gr, const native_handle_t *buffer)
{
   hidl_vec<uint8_t> encoded_layouts;

   Error error = GetMetadata(gr, buffer,
                             android::gralloc4::MetadataType_PlaneLayouts,
                             &encoded_layouts);

   if (error != Error::NONE)
      return std::nullopt;

   std::vector<PlaneLayout> plane_layouts;

   auto status =
      android::gralloc4::decodePlaneLayouts(encoded_layouts, &plane_layouts);

   if (status != android::OK)
      return std::nullopt;

   return plane_layouts;
}

extern "C" {

/* Whether fd is something with a size (a dma-buf), leaving its offset at 0. */
static bool
mapper4_fd_has_size(int fd)
{
   const off_t sz = lseek(fd, 0, SEEK_END);
   lseek(fd, 0, SEEK_SET);
   return sz > 0;
}

static int
mapper4_query_buffer_basic_info(struct u_gralloc *gralloc,
                                struct u_gralloc_buffer_handle *hnd,
                                struct u_gralloc_buffer_basic_info *out)
{
   gralloc4 *gr4 = (gralloc4 *)gralloc;

   if (gr4->mapper == nullptr && !gr4->mapper5)
      return -EINVAL;

   if (!hnd->handle)
      return -EINVAL;

   hidl_vec<uint8_t> encoded_format;
   auto err = GetMetadata(gr4, hnd->handle,
                          android::gralloc4::MetadataType_PixelFormatFourCC,
                          &encoded_format);
   if (err != Error::NONE)
      return -EINVAL;

   uint32_t drm_fourcc;

   auto status =
      android::gralloc4::decodePixelFormatFourCC(encoded_format, &drm_fourcc);
   if (status != android::OK)
      return -EINVAL;

   uint64_t drm_modifier;

   hidl_vec<uint8_t> encoded_modifier;
   err = GetMetadata(gr4, hnd->handle,
                     android::gralloc4::MetadataType_PixelFormatModifier,
                     &encoded_modifier);
   if (err != Error::NONE)
      return -EINVAL;

   status = android::gralloc4::decodePixelFormatModifier(encoded_modifier,
                                                         &drm_modifier);
   if (status != android::OK)
      return -EINVAL;

   uint64_t alloc_size;

   hidl_vec<uint8_t> encoded_alloc_size;
   err = GetMetadata(gr4, hnd->handle,
                     android::gralloc4::MetadataType_AllocationSize,
                     &encoded_alloc_size);
   if (err != Error::NONE)
      return -EINVAL;

   status = android::gralloc4::decodeAllocationSize(encoded_alloc_size,
                                                    &alloc_size);
   if (status != android::OK)
      return -EINVAL;

   uint64_t layer_count;

   hidl_vec<uint8_t> encoded_layer_count;
   err = GetMetadata(gr4, hnd->handle,
                     android::gralloc4::MetadataType_LayerCount,
                     &encoded_layer_count);
   if (err != Error::NONE)
      return -EINVAL;

   status = android::gralloc4::decodeLayerCount(encoded_layer_count,
                                                &layer_count);
   if (status != android::OK)
      return -EINVAL;

   out->drm_fourcc = drm_fourcc;
   out->modifier = drm_modifier;
   out->alloc_size = alloc_size;
   out->layer_count = layer_count;

   auto layouts_opt = GetPlaneLayouts(gr4, hnd->handle);

   if (!layouts_opt)
      return -EINVAL;

   std::vector<PlaneLayout> &layouts = *layouts_opt;

   if (layouts.empty() || layouts.size() > ARRAY_SIZE(out->fds))
      return -EINVAL;
   out->num_planes = layouts.size();

   int fd_index = 0;

   /* XCLIPSE: some gralloc4 implementations (Samsung's) leave strideInBytes at 0 on linear
    * buffers while every other field is right. Rebuild it from the plane's width and component
    * sizes, or from its total size when it is the only plane. */
   for (uint32_t i = 0; i < layouts.size(); i++) {
      if (layouts[i].strideInBytes != 0)
         continue;

      int64_t bits = 0;
      for (const auto &comp : layouts[i].components)
         bits += comp.sizeInBits;

      int64_t stride = 0;
      if (bits > 0 && bits % 8 == 0 && layouts[i].widthInSamples > 0)
         stride = layouts[i].widthInSamples * (bits / 8);
      else if (layouts.size() == 1 && layouts[i].heightInSamples > 0)
         stride = layouts[i].totalSizeInBytes / layouts[i].heightInSamples;
      if (stride <= 0)
         return -EINVAL;
      layouts[i].strideInBytes = stride;
   }

   for (uint32_t i = 0; i < layouts.size(); i++) {
      out->strides[i] = layouts[i].strideInBytes;
      out->offsets[i] = layouts[i].offsetInBytes;

      /* offset == 0 means layer is located in different dma-buf */
      if (out->offsets[i] == 0 && i > 0)
         fd_index++;

      if (fd_index >= hnd->handle->numFds)
         return -EINVAL;

      /* XCLIPSE: MediaTek's ARM gralloc puts a -1 fd ahead of the buffer's, or (Dimensity 9300,
       * Android 15) an fd that is not a dma-buf at all: lseek gives it no size. Skip both. */
      while (fd_index < hnd->handle->numFds &&
             (hnd->handle->data[fd_index] < 0 || !mapper4_fd_has_size(hnd->handle->data[fd_index])))
         fd_index++;
      if (fd_index >= hnd->handle->numFds)
         return -EINVAL;

      out->fds[i] = hnd->handle->data[fd_index];
   }

   return 0;
}

static int
mapper4_get_buffer_basic_info(struct u_gralloc *gralloc,
                              struct u_gralloc_buffer_handle *hnd,
                              struct u_gralloc_buffer_basic_info *out)
{
   gralloc4 *gr4 = (gralloc4 *)gralloc;

   int ret = mapper4_query_buffer_basic_info(gralloc, hnd, out);
   if (ret == 0 || !gr4->fallback || !hnd->handle)
      return ret;

   memset(out, 0, sizeof(*out));
   return gr4->fallback->ops.get_buffer_basic_info(gr4->fallback, hnd, out);
}

static int
mapper4_get_buffer_color_info(struct u_gralloc *gralloc,
                              struct u_gralloc_buffer_handle *hnd,
                              struct u_gralloc_buffer_color_info *out)
{
   gralloc4 *gr4 = (gralloc4 *)gralloc;

   if (gr4->mapper == nullptr)
      return -EINVAL;

   if (!hnd->handle)
      return -EINVAL;

   /* optional attributes */
   hidl_vec<uint8_t> encoded_chroma_siting;
   std::optional<ChromaSiting> chroma_siting;
   auto err = GetMetadata(gr4, hnd->handle,
                          android::gralloc4::MetadataType_ChromaSiting,
                          &encoded_chroma_siting);
   if (err == Error::NONE) {
      ExtendableType chroma_siting_ext;
      auto status = android::gralloc4::decodeChromaSiting(
         encoded_chroma_siting, &chroma_siting_ext);
      if (status != android::OK)
         return -EINVAL;

      chroma_siting =
         android::gralloc4::getStandardChromaSitingValue(chroma_siting_ext);
   }

   hidl_vec<uint8_t> encoded_dataspace;
   err = GetMetadata(gr4, hnd->handle,
                     android::gralloc4::MetadataType_Dataspace,
                     &encoded_dataspace);
   if (err == Error::NONE) {
      Dataspace dataspace;
      auto status =
         android::gralloc4::decodeDataspace(encoded_dataspace, &dataspace);
      if (status != android::OK)
         return -EINVAL;

      /* default to __DRI_YUV_COLOR_SPACE_ITU_REC601 */
      Dataspace standard =
         (Dataspace)((int)dataspace & (uint32_t)Dataspace::STANDARD_MASK);
      switch (standard) {
      case Dataspace::STANDARD_BT709:
         out->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC709;
         break;
      case Dataspace::STANDARD_BT2020:
      case Dataspace::STANDARD_BT2020_CONSTANT_LUMINANCE:
         out->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC2020;
         break;
      case Dataspace::STANDARD_BT601_625:
      case Dataspace::STANDARD_BT601_625_UNADJUSTED:
      case Dataspace::STANDARD_BT601_525:
      case Dataspace::STANDARD_BT601_525_UNADJUSTED:
      default:
         out->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC601;
         break;
      }

      /* default to __DRI_YUV_NARROW_RANGE */
      Dataspace range =
         (Dataspace)((int)dataspace & (uint32_t)Dataspace::RANGE_MASK);
      switch (range) {
      case Dataspace::RANGE_FULL:
         out->sample_range = __DRI_YUV_FULL_RANGE;
         break;
      case Dataspace::RANGE_LIMITED:
      default:
         out->sample_range = __DRI_YUV_NARROW_RANGE;
         break;
      }
   }

   /* default to __DRI_YUV_CHROMA_SITING_0_5 */
   if (chroma_siting) {
      switch (*chroma_siting) {
      case ChromaSiting::COSITED_HORIZONTAL:
         out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0;
         out->vertical_siting = __DRI_YUV_CHROMA_SITING_0_5;
         break;
#if ANDROID_API_LEVEL >= 35
      case ChromaSiting::COSITED_VERTICAL:
         out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0_5;
         out->vertical_siting = __DRI_YUV_CHROMA_SITING_0;
         break;
      case ChromaSiting::COSITED_BOTH:
         out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0;
         out->vertical_siting = __DRI_YUV_CHROMA_SITING_0;
         break;
#endif
      case ChromaSiting::SITED_INTERSTITIAL:
      default:
         out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0_5;
         out->vertical_siting = __DRI_YUV_CHROMA_SITING_0_5;
         break;
      }
   }

   return 0;
}

static int
mapper4_get_front_rendering_usage(struct u_gralloc *gralloc,
                                  uint64_t *out_usage)
{
   assert(out_usage);
#if ANDROID_API_LEVEL >= 33
   *out_usage = static_cast<uint64_t>(BufferUsage::FRONT_BUFFER);

   return 0;
#else
   return -ENOTSUP;
#endif
}

static int
destroy(struct u_gralloc *gralloc)
{
   gralloc4 *gr = (struct gralloc4 *)gralloc;
   if (gr->fallback)
      gr->fallback->ops.destroy(gr->fallback);
   if (gr->mapper5_library)
      dlclose(gr->mapper5_library);
   delete gr;

   return 0;
}

struct u_gralloc *
u_gralloc_imapper_api_create()
{
   auto gr = new gralloc4;
   gr->mapper5 = LoadMapper5(&gr->mapper5_library);
   if (!gr->mapper5)
      gr->mapper = IMapper::getService();
   if (!gr->mapper5 && !gr->mapper) {
      delete gr;
      return NULL;
   }
   gr->fallback = u_gralloc_fallback_create();
   gr->base.ops.get_buffer_basic_info = mapper4_get_buffer_basic_info;
   gr->base.ops.get_buffer_color_info = mapper4_get_buffer_color_info;
   gr->base.ops.get_front_rendering_usage = mapper4_get_front_rendering_usage;
   gr->base.ops.destroy = destroy;

   mesa_logi("Using IMapper v%d API", gr->mapper5 ? 5 : 4);

   return &gr->base;
}

} // extern "C"
