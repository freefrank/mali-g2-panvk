#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <dlfcn.h>
#include <dirent.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
   fprintf(stderr, "%s failed: %d\n", #call, r); return 1; } } while (0)
#define LOAD(name) PFN_##name name = (PFN_##name)get(instance, #name); \
   if (!name) { fprintf(stderr, "%s missing\n", #name); return 1; }

static unsigned
udp_sockets(void)
{
   DIR *dir=opendir("/proc/self/fd");
   if (!dir) return 0;
   unsigned count=0;
   struct dirent *entry;
   while ((entry=readdir(dir))) {
      char *end;
      long fd=strtol(entry->d_name,&end,10);
      if (!entry->d_name[0] || *end || fd<0) continue;
      int type=0; socklen_t size=sizeof(type);
      struct sockaddr_storage address; socklen_t length=sizeof(address);
      if (!getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size) && type==SOCK_DGRAM &&
          !getsockname(fd,(struct sockaddr *)&address,&length) && address.ss_family==AF_INET) count++;
   }
   closedir(dir);
   return count;
}

static int
compare_double(const void *a, const void *b)
{
   double x = *(const double *)a, y = *(const double *)b;
   return (x > y) - (x < y);
}

static uint32_t
memory_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits,
            VkMemoryPropertyFlags flags)
{
   for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
      if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & flags) == flags)
         return i;
   fprintf(stderr, "No suitable memory type\n");
   exit(1);
}

static uint32_t
pattern(uint32_t x, uint32_t y, uint32_t frame)
{
   return ((x * 13 + y * 3 + frame * 17) & 255) |
          ((x * 5 + y * 19 + frame * 29) & 255) << 8 |
          ((x * 7 + y * 11 + frame * 43) & 255) << 16 |
          ((x * 23 + y * 31 + frame * 53) & 255) << 24;
}

static uint32_t
block_color(VkFormat format, uint32_t x, uint32_t y, uint32_t frame)
{
   uint32_t c = pattern(x, y, frame);
   if (format == VK_FORMAT_ASTC_4x4_UNORM_BLOCK) return c;
   if (format == VK_FORMAT_BC7_UNORM_BLOCK) return c & 0xfefefefe;
   static const uint8_t five[8] = {0, 4, 8, 12, 19, 23, 27, 31};
   static const uint8_t six[8] = {0, 9, 18, 27, 36, 45, 54, 63};
   uint32_t r = five[(c >> 2) & 7], g = six[(c >> 10) & 7];
   uint32_t b = five[(c >> 18) & 7];
   uint32_t a = format == VK_FORMAT_BC1_RGB_UNORM_BLOCK ? 255 : c >> 24;
   return ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) |
      (((b << 3) | (b >> 2)) << 16) | a << 24;
}

static void
put_bits(uint8_t *p, unsigned *offset, unsigned count, uint32_t value)
{
   for (unsigned i = 0; i < count; i++, (*offset)++)
      p[*offset / 8] |= ((value >> i) & 1) << (*offset % 8);
}

static uint32_t
unorm8(float x)
{
   return !(x > 0.0f) ? 0 : x >= 1.0f ? 255 : (uint32_t)(x * 255.0f + 0.5f);
}

static float
ufloat(uint32_t value, unsigned mantissa_bits)
{
   unsigned exponent = value >> mantissa_bits;
   unsigned mantissa = value & ((1u << mantissa_bits) - 1);
   if (!exponent) return (float)mantissa / (1u << (14 + mantissa_bits));
   uint32_t bits = (exponent == 31 ? 255 : exponent + 112) << 23 |
      mantissa << (23 - mantissa_bits);
   float result;
   memcpy(&result, &bits, 4);
   return result;
}

static uint32_t
read_render_pixel(const void *memory, size_t pixel, VkFormat format)
{
   if (format == VK_FORMAT_R16G16B16A16_UNORM || format == VK_FORMAT_R16G16B16A16_SFLOAT ||
       format == VK_FORMAT_R32G32B32A32_SFLOAT) {
      uint32_t result = 0;
      for (unsigned c = 0; c < 4; c++) {
         float value;
         if (format == VK_FORMAT_R32G32B32A32_SFLOAT) value = ((const float *)memory)[pixel * 4 + c];
         else if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            _Float16 half;
            memcpy(&half, (const uint8_t *)memory + pixel * 8 + c * 2, 2);
            value = half;
         } else value = (float)((const uint16_t *)memory)[pixel * 4 + c] / 65535.0f;
         result |= unorm8(value) << (c * 8);
      }
      return result;
   }
   uint32_t v = ((const uint32_t *)memory)[pixel];
   if (format == VK_FORMAT_B8G8R8A8_UNORM)
      return (v & 0xff00ff00) | (v & 255) << 16 | (v >> 16 & 255);
   if (format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)
      return (((v >> 20) & 1023) * 255 + 511) / 1023 |
         ((((v >> 10) & 1023) * 255 + 511) / 1023) << 8 |
         (((v & 1023) * 255 + 511) / 1023) << 16 | (v >> 30) * 85 << 24;
   if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32)
      return unorm8(ufloat(v & 2047, 6)) | unorm8(ufloat(v >> 11 & 2047, 6)) << 8 |
         unorm8(ufloat(v >> 22, 5)) << 16 | 0xff000000;
   return v;
}

int
main(int argc, char **argv)
{
   setbuf(stdout, NULL);
   int audit=getenv("MALI_G2_TILES_AUDIT_DIAGNOSTICS") != NULL;
   const int audit_signals[5]={SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGABRT};
   struct sigaction before[5];
   unsigned sockets_before=0;
   if (audit) {
      sockets_before=udp_sockets();
      for (unsigned i=0;i<5;i++) sigaction(audit_signals[i],NULL,&before[i]);
   }
   if (argc < 6 || (argc > 7 && argc != 9)) {
      fprintf(stderr, "usage: %s driver sample.spv width height frames [VkFormat [vertex.spv fragment.spv]]\n", argv[0]);
      return 2;
   }
   int graphics = argc == 9;
   int timing = getenv("MALI_G2_TILES_TIMING") != NULL;
   const char *compression_mode = getenv("MALI_G2_TILES_COMPRESSION");
   int blend_enabled = getenv("MALI_G2_TILES_BLEND") != NULL;
   int scissor_enabled = getenv("MALI_G2_TILES_SCISSOR") != NULL;
   int load_enabled = getenv("MALI_G2_TILES_LOAD") != NULL;
   int load_draw = getenv("MALI_G2_TILES_LOAD_DRAW") != NULL;
   int partial_render = getenv("MALI_G2_TILES_PARTIAL_RENDER") != NULL;
   int rebind_load = getenv("MALI_G2_TILES_REBIND") != NULL;
   int alpha_r = getenv("MALI_G2_TILES_ALPHA_R") != NULL;
   int discard_enabled = getenv("MALI_G2_TILES_DISCARD") != NULL;
   int dual_source = getenv("MALI_G2_TILES_DUAL_SOURCE") != NULL;
   uint32_t binding_base = getenv("MALI_G2_TILES_BINDING") ? strtoul(getenv("MALI_G2_TILES_BINDING"), NULL, 10) : 0;
   int tint_enabled = getenv("MALI_G2_TILES_TINT") != NULL;
   int white_background = getenv("MALI_G2_TILES_WHITE") != NULL;
   int image_copy = getenv("MALI_G2_TILES_IMAGE_COPY") != NULL;
   int copy_via_rgba16 = getenv("MALI_G2_TILES_COPY_VIA_RGBA16") != NULL;
   int split_upload = getenv("MALI_G2_TILES_SPLIT_UPLOAD") != NULL;
   VkFormat depth_format = getenv("MALI_G2_TILES_DEPTH_FORMAT") ?
      strtoul(getenv("MALI_G2_TILES_DEPTH_FORMAT"), NULL, 10) : VK_FORMAT_UNDEFINED;
   int depth_roundtrip = getenv("MALI_G2_TILES_DEPTH_ROUNDTRIP") != NULL;
   int depth_readback = depth_roundtrip || getenv("MALI_G2_TILES_DEPTH_READBACK") != NULL;
   if (depth_readback && (!graphics || !depth_format)) return 2;
   int reverse_depth = getenv("MALI_G2_TILES_REVERSE_DEPTH") != NULL;
   int reject_depth = getenv("MALI_G2_TILES_REJECT_DEPTH") != NULL;
   uint32_t index_bits = getenv("MALI_G2_TILES_INDEX_BITS") ?
      strtoul(getenv("MALI_G2_TILES_INDEX_BITS"), NULL, 10) : 0;
   int32_t vertex_offset = getenv("MALI_G2_TILES_VERTEX_OFFSET") ?
      strtol(getenv("MALI_G2_TILES_VERTEX_OFFSET"), NULL, 10) : 0;
   if (index_bits && (!graphics || (index_bits != 16 && index_bits != 32) ||
       vertex_offset > 0 || vertex_offset < -32767)) return 2;
   if (depth_format && (!graphics || (depth_format != VK_FORMAT_D16_UNORM &&
       depth_format != VK_FORMAT_D32_SFLOAT && depth_format != VK_FORMAT_D24_UNORM_S8_UINT &&
       depth_format != VK_FORMAT_D32_SFLOAT_S8_UINT))) return 2;
   VkImageAspectFlags depth_aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
   if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT || depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      depth_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
   if (binding_base > 8 || (tint_enabled && binding_base < 2)) return 2;
   VkFormat render_format = getenv("MALI_G2_TILES_RT_FORMAT") ?
      strtoul(getenv("MALI_G2_TILES_RT_FORMAT"), NULL, 10) : VK_FORMAT_R8G8B8A8_UNORM;
   if (render_format != VK_FORMAT_R8G8B8A8_UNORM && render_format != VK_FORMAT_B8G8R8A8_UNORM &&
       render_format != VK_FORMAT_A2R10G10B10_UNORM_PACK32 && render_format != VK_FORMAT_R16G16B16A16_UNORM &&
       render_format != VK_FORMAT_R16G16B16A16_SFLOAT && render_format != VK_FORMAT_R32G32B32A32_SFLOAT &&
       render_format != VK_FORMAT_B10G11R11_UFLOAT_PACK32) return 2;
   uint32_t render_bpp = render_format == VK_FORMAT_R32G32B32A32_SFLOAT ? 16 :
      (render_format == VK_FORMAT_R16G16B16A16_UNORM || render_format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : 4);
   uint32_t w = strtoul(argv[3], NULL, 10), h = strtoul(argv[4], NULL, 10);
   uint32_t frames = strtoul(argv[5], NULL, 10);
   uint32_t mip = getenv("MALI_G2_TILES_MIP") ? strtoul(getenv("MALI_G2_TILES_MIP"), NULL, 10) : 0;
   uint32_t layer = getenv("MALI_G2_TILES_LAYER") ? strtoul(getenv("MALI_G2_TILES_LAYER"), NULL, 10) : 0;
   VkFormat format = argc > 6 ? strtoul(argv[6], NULL, 10) : VK_FORMAT_R8G8B8A8_UNORM;
   if (copy_via_rgba16 && (!image_copy || format != VK_FORMAT_BC1_RGB_UNORM_BLOCK || mip)) return 2;
   int compressed = format == VK_FORMAT_BC1_RGB_UNORM_BLOCK || format == VK_FORMAT_BC3_UNORM_BLOCK ||
      format == VK_FORMAT_BC7_UNORM_BLOCK || format == VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
   int wide = format == VK_FORMAT_R16G16B16A16_UNORM || format == VK_FORMAT_R16G16B16A16_SFLOAT ||
      format == VK_FORMAT_R32G32B32A32_SFLOAT;
   if (!w || !h || w > 4096 || h > 4096 || !frames || frames > 1000 ||
       mip > 4 || layer > 4 || (w << mip) > 16384 || (h << mip) > 16384 ||
       (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_B8G8R8A8_UNORM &&
        format != VK_FORMAT_R5G5B5A1_UNORM_PACK16 && format != VK_FORMAT_R5G6B5_UNORM_PACK16 &&
        format != VK_FORMAT_A1R5G5B5_UNORM_PACK16 && format != VK_FORMAT_R8_UNORM &&
        format != VK_FORMAT_R8G8_UNORM && !compressed && !wide))
      return 2;
   uint32_t bpp = format == VK_FORMAT_R32G32B32A32_SFLOAT ? 16 : wide ? 8 : format == VK_FORMAT_R8_UNORM ? 1 :
      (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM ? 4 : 2);
   size_t pixels = (size_t)w * h;
   uint32_t blocks_w = (w + 3) / 4, blocks_h = (h + 3) / 4;
   uint32_t block_bytes = format == VK_FORMAT_BC1_RGB_UNORM_BLOCK ? 8 : 16;
   size_t raw_size = compressed ? (size_t)blocks_w * blocks_h * block_bytes : pixels * bpp;
   void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr get = dlsym(library, "vk_icdGetInstanceProcAddr");
   if (!get) get = dlsym(library, "vkGetInstanceProcAddr");
   PFN_vkCreateInstance create = NULL;
   if (!get) {
      hwvulkan_module_t *module = dlsym(library, HAL_MODULE_INFO_SYM_AS_STR);
      hwvulkan_device_t *hal = NULL;
      if (!module || module->common.methods->open(&module->common, HWVULKAN_DEVICE_0,
                                                 (hw_device_t **)&hal))
         return 1;
      get = hal->GetInstanceProcAddr;
      create = hal->CreateInstance;
   }
   if (!create) create = (PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "g2_texture_tiles", .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app};
   VkInstance instance;
   CHECK(create(&ici, NULL, &instance));
   LOAD(vkEnumeratePhysicalDevices); LOAD(vkGetPhysicalDeviceMemoryProperties);
   LOAD(vkGetPhysicalDeviceFeatures);
   LOAD(vkGetPhysicalDeviceProperties);
   LOAD(vkGetPhysicalDeviceFeatures2);
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties); LOAD(vkCreateDevice);
   LOAD(vkGetDeviceQueue); LOAD(vkCreateBuffer); LOAD(vkGetBufferMemoryRequirements);
   LOAD(vkAllocateMemory); LOAD(vkBindBufferMemory); LOAD(vkMapMemory);
   LOAD(vkCreateImage); LOAD(vkGetImageMemoryRequirements); LOAD(vkBindImageMemory);
   LOAD(vkCreateImageView); LOAD(vkCreateSampler); LOAD(vkCreateShaderModule);
   LOAD(vkCreateDescriptorSetLayout); LOAD(vkCreatePipelineLayout);
   LOAD(vkCreateComputePipelines); LOAD(vkCreateDescriptorPool);
   LOAD(vkCreateRenderPass); LOAD(vkCreateFramebuffer); LOAD(vkCreateGraphicsPipelines);
   LOAD(vkCmdBeginRenderPass); LOAD(vkCmdEndRenderPass); LOAD(vkCmdDraw); LOAD(vkCmdSetScissor);
   LOAD(vkCmdBindIndexBuffer); LOAD(vkCmdDrawIndexed);
   LOAD(vkDestroyRenderPass); LOAD(vkDestroyFramebuffer);
   LOAD(vkAllocateDescriptorSets); LOAD(vkUpdateDescriptorSets);
   LOAD(vkCreateCommandPool); LOAD(vkAllocateCommandBuffers);
   LOAD(vkBeginCommandBuffer); LOAD(vkEndCommandBuffer); LOAD(vkResetCommandBuffer);
   LOAD(vkCmdPipelineBarrier); LOAD(vkCmdCopyBufferToImage); LOAD(vkCmdCopyImageToBuffer);
   LOAD(vkCmdClearDepthStencilImage);
   LOAD(vkCmdCopyImage);
   LOAD(vkCmdBindPipeline); LOAD(vkCmdBindDescriptorSets); LOAD(vkCmdPushConstants);
   LOAD(vkCmdDispatch); LOAD(vkQueueSubmit); LOAD(vkQueueWaitIdle);
   LOAD(vkUnmapMemory); LOAD(vkDestroyBuffer); LOAD(vkFreeMemory);
   LOAD(vkDestroyImageView); LOAD(vkDestroyImage); LOAD(vkDestroySampler);
   LOAD(vkDestroyDescriptorPool); LOAD(vkDestroyDescriptorSetLayout);
   LOAD(vkDestroyPipeline); LOAD(vkDestroyPipelineLayout); LOAD(vkDestroyShaderModule);
   LOAD(vkDestroyCommandPool); LOAD(vkDestroyDevice); LOAD(vkDestroyInstance);
   LOAD(vkCreateQueryPool); LOAD(vkDestroyQueryPool); LOAD(vkGetQueryPoolResults);
   LOAD(vkCmdResetQueryPool); LOAD(vkCmdWriteTimestamp);
   uint32_t count = 1;
   VkPhysicalDevice physical;
   CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
   if (!count) return 1;
   uint32_t family_count = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
   VkQueueFamilyProperties *families = calloc(family_count, sizeof(*families));
   if (!families) return 1;
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
   uint32_t family = 0;
   VkQueueFlags required = VK_QUEUE_COMPUTE_BIT | (graphics ? VK_QUEUE_GRAPHICS_BIT : 0);
   while (family < family_count && (families[family].queueFlags & required) != required) family++;
   free(families);
   if (family == family_count) return 1;
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
   const char *compression_extension = VK_EXT_IMAGE_COMPRESSION_CONTROL_EXTENSION_NAME;
   VkPhysicalDeviceImageCompressionControlFeaturesEXT compression_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT,
   };
   if (compression_mode) {
      VkPhysicalDeviceFeatures2 features = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &compression_features,
      };
      vkGetPhysicalDeviceFeatures2(physical, &features);
      if (!compression_features.imageCompressionControl) return 2;
      dci.pNext = &compression_features;
      dci.enabledExtensionCount = 1;
      dci.ppEnabledExtensionNames = &compression_extension;
   }
   VkPhysicalDeviceFeatures supported, enabled = {0};
   vkGetPhysicalDeviceFeatures(physical, &supported);
   if (compressed) {
      if (format == VK_FORMAT_ASTC_4x4_UNORM_BLOCK) {
         if (!supported.textureCompressionASTC_LDR) return 2;
         enabled.textureCompressionASTC_LDR = VK_TRUE;
      } else {
         if (!supported.textureCompressionBC) return 2;
         enabled.textureCompressionBC = VK_TRUE;
      }
      dci.pEnabledFeatures = &enabled;
   }
   if (dual_source) {
      if (!supported.dualSrcBlend) return 2;
      enabled.dualSrcBlend = VK_TRUE;
      dci.pEnabledFeatures = &enabled;
   }
   VkDevice device;
   CHECK(vkCreateDevice(physical, &dci, NULL, &device));
   VkQueue queue;
   vkGetDeviceQueue(device, family, 0, &queue);
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(physical, &mp);
   VkBuffer buffers[8];
   VkDeviceMemory allocations[8];
   void *mapped[8];
   unsigned buffer_count = depth_roundtrip ? 8 : depth_readback ? 7 : index_bits ? 6 : binding_base ? 5 : graphics ? 4 : 3;
   VkDeviceSize sizes[8] = {raw_size, raw_size, pixels * 4, pixels * render_bpp, 16, 128, pixels * 8, pixels * 8};
   for (unsigned i = 0; i < buffer_count; i++) {
      VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = sizes[i], .usage = i == 5 ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT :
            i == 4 ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : i == 0 ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT :
            (i == 2 ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : VK_BUFFER_USAGE_TRANSFER_DST_BIT)};
      if (depth_roundtrip && i == 6) bci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      CHECK(vkCreateBuffer(device, &bci, NULL, &buffers[i]));
      VkMemoryRequirements mr;
      vkGetBufferMemoryRequirements(device, buffers[i], &mr);
      VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = mr.size, .memoryTypeIndex = memory_type(&mp, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
      CHECK(vkAllocateMemory(device, &ai, NULL, &allocations[i]));
      CHECK(vkBindBufferMemory(device, buffers[i], allocations[i], 0));
      CHECK(vkMapMemory(device, allocations[i], 0, sizes[i], 0, &mapped[i]));
   }
   if (index_bits) {
      memset(mapped[5], 0xff, sizes[5]);
      for (unsigned i = 0; i < 3; i++) {
         if (index_bits == 16) ((uint16_t *)mapped[5])[7 + i] = i - vertex_offset;
         else ((uint32_t *)mapped[5])[7 + i] = i - vertex_offset;
      }
   }
   VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = getenv("MALI_G2_TILES_CREATE_FLAGS") ?
         strtoul(getenv("MALI_G2_TILES_CREATE_FLAGS"), NULL, 0) : 0,
      .imageType = VK_IMAGE_TYPE_2D, .format = format, .extent = {w << mip, h << mip, 1},
      .mipLevels = mip + 1, .arrayLayers = layer + 1, .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
   VkImageCompressionControlEXT compression_control = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT,
      .flags = VK_IMAGE_COMPRESSION_DISABLED_EXT,
   };
   if (compression_mode && !strcmp(compression_mode, "off"))
      ii.pNext = &compression_control;
   if (compressed) ii.usage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
   VkImage image;
   CHECK(vkCreateImage(device, &ii, NULL, &image));
   VkMemoryRequirements mr;
   vkGetImageMemoryRequirements(device, image, &mr);
   const VkDeviceSize image_allocation_size = mr.size;
   VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size, .memoryTypeIndex = memory_type(&mp, mr.memoryTypeBits,
         getenv("MALI_G2_TILES_DUMP_IMAGE") ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0)};
   VkDeviceMemory image_memory;
   CHECK(vkAllocateMemory(device, &ai, NULL, &image_memory));
   CHECK(vkBindImageMemory(device, image, image_memory, 0));
   void *image_mapped = NULL;
   if (getenv("MALI_G2_TILES_DUMP_IMAGE"))
      CHECK(vkMapMemory(device, image_memory, 0, VK_WHOLE_SIZE, 0, &image_mapped));
   if (compression_mode) {
      PFN_vkGetImageSubresourceLayout2EXT query_compression =
         (PFN_vkGetImageSubresourceLayout2EXT)get(instance, "vkGetImageSubresourceLayout2EXT");
      if (!query_compression) return 2;
      VkImageSubresource2EXT subresource = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT,
         .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer},
      };
      VkImageCompressionPropertiesEXT properties = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT,
      };
      VkSubresourceLayout2EXT layout = {
         .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT,
         .pNext = &properties,
      };
      query_compression(device, image, &subresource, &layout);
      printf("TILES_COMPRESSION flags=%#x allocation=%llu\n",
             properties.imageCompressionFlags, (unsigned long long)mr.size);
   }
   VkImage sampled_image = image, copy_image = VK_NULL_HANDLE;
   VkDeviceMemory copy_memory = VK_NULL_HANDLE;
   VkImage via_image = VK_NULL_HANDLE;
   VkDeviceMemory via_memory = VK_NULL_HANDLE;
   if (image_copy) {
      if (getenv("MALI_G2_TILES_COPY_FLAGS"))
         ii.flags = strtoul(getenv("MALI_G2_TILES_COPY_FLAGS"), NULL, 0);
      CHECK(vkCreateImage(device, &ii, NULL, &copy_image));
      vkGetImageMemoryRequirements(device, copy_image, &mr);
      ai.allocationSize = mr.size;
      ai.memoryTypeIndex = memory_type(&mp, mr.memoryTypeBits, 0);
      CHECK(vkAllocateMemory(device, &ai, NULL, &copy_memory));
      CHECK(vkBindImageMemory(device, copy_image, copy_memory, 0));
      sampled_image = copy_image;
   }
   if (copy_via_rgba16) {
      VkImageCreateInfo via_info = ii;
      via_info.flags = 0;
      via_info.format = VK_FORMAT_R16G16B16A16_UNORM;
      via_info.extent = (VkExtent3D){(w + 3) / 4, (h + 3) / 4, 1};
      via_info.mipLevels = 1;
      via_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      CHECK(vkCreateImage(device, &via_info, NULL, &via_image));
      vkGetImageMemoryRequirements(device, via_image, &mr);
      ai.allocationSize = mr.size;
      ai.memoryTypeIndex = memory_type(&mp, mr.memoryTypeBits, 0);
      CHECK(vkAllocateMemory(device, &ai, NULL, &via_memory));
      CHECK(vkBindImageMemory(device, via_image, via_memory, 0));
      printf("TILES_COPY_VIA_RGBA16\n");
      if (compression_mode) {
         PFN_vkGetImageSubresourceLayout2EXT query =
            (PFN_vkGetImageSubresourceLayout2EXT)get(instance, "vkGetImageSubresourceLayout2EXT");
         if (!query) return 2;
         VkImageSubresource2EXT sub = {.sType = VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer}};
         VkImageCompressionPropertiesEXT props = {.sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT};
         VkSubresourceLayout2EXT output = {.sType = VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT, .pNext = &props};
         query(device, via_image, &sub, &output);
         printf("VIA_COMPRESSION flags=%#x\n", props.imageCompressionFlags);
      }
   }
   VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = sampled_image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, layer, 1}};
   if (alpha_r) vi.components = (VkComponentMapping){VK_COMPONENT_SWIZZLE_ONE,
      VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_R};
   VkImageView view;
   CHECK(vkCreateImageView(device, &vi, NULL, &view));
   VkImage render_image = VK_NULL_HANDLE;
   VkDeviceMemory render_memory = VK_NULL_HANDLE;
   VkImageView render_view = VK_NULL_HANDLE;
   VkImage depth_image = VK_NULL_HANDLE;
   VkDeviceMemory depth_memory = VK_NULL_HANDLE;
   VkImageView depth_view = VK_NULL_HANDLE;
   VkRenderPass render_pass = VK_NULL_HANDLE;
   VkRenderPass load_pass = VK_NULL_HANDLE;
   VkFramebuffer framebuffer = VK_NULL_HANDLE;
   if (graphics) {
      VkImageCreateInfo ri = ii;
      ri.flags = 0;
      ri.format = render_format;
      ri.extent = (VkExtent3D){w, h, 1};
      ri.mipLevels = 1;
      ri.arrayLayers = 1;
      ri.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      CHECK(vkCreateImage(device, &ri, NULL, &render_image));
      vkGetImageMemoryRequirements(device, render_image, &mr);
      ai.allocationSize = mr.size;
      ai.memoryTypeIndex = memory_type(&mp, mr.memoryTypeBits, 0);
      CHECK(vkAllocateMemory(device, &ai, NULL, &render_memory));
      CHECK(vkBindImageMemory(device, render_image, render_memory, 0));
      VkImageViewCreateInfo rv = vi;
      rv.image = render_image;
      rv.format = ri.format;
      rv.components = (VkComponentMapping){0};
      rv.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      CHECK(vkCreateImageView(device, &rv, NULL, &render_view));
      if (depth_format) {
         ri.format = depth_format;
         ri.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
         if (depth_readback) ri.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
         if (depth_roundtrip) ri.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
         CHECK(vkCreateImage(device, &ri, NULL, &depth_image));
         vkGetImageMemoryRequirements(device, depth_image, &mr);
         ai.allocationSize = mr.size;
         ai.memoryTypeIndex = memory_type(&mp, mr.memoryTypeBits, 0);
         CHECK(vkAllocateMemory(device, &ai, NULL, &depth_memory));
         CHECK(vkBindImageMemory(device, depth_image, depth_memory, 0));
         if (compression_mode) {
            PFN_vkGetImageSubresourceLayout2EXT query =
               (PFN_vkGetImageSubresourceLayout2EXT)get(instance, "vkGetImageSubresourceLayout2EXT");
            if (!query) return 2;
            VkImageSubresource2EXT subresource = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT,
               .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0},
            };
            VkImageCompressionPropertiesEXT properties = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT,
            };
            VkSubresourceLayout2EXT layout = {
               .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT,
               .pNext = &properties,
            };
            query(device, depth_image, &subresource, &layout);
            printf("DEPTH_COMPRESSION format=%u flags=%#x allocation=%llu\n",
                   depth_format, properties.imageCompressionFlags, (unsigned long long)mr.size);
         }
         rv.image = depth_image;
         rv.format = depth_format;
         rv.subresourceRange.aspectMask = depth_aspects;
         CHECK(vkCreateImageView(device, &rv, NULL, &depth_view));
      }
      VkAttachmentDescription attachments[2] = {{.format = render_format, .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
         {.format = depth_format, .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
          .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
      VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
      VkAttachmentReference depth_reference = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
      VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
         .colorAttachmentCount = 1, .pColorAttachments = &reference,
         .pDepthStencilAttachment = depth_format ? &depth_reference : NULL};
      VkRenderPassCreateInfo rpi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
         .attachmentCount = depth_format ? 2 : 1, .pAttachments = attachments,
         .subpassCount = 1, .pSubpasses = &subpass};
      CHECK(vkCreateRenderPass(device, &rpi, NULL, &render_pass));
      attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      CHECK(vkCreateRenderPass(device, &rpi, NULL, &load_pass));
      VkImageView attachment_views[2] = {render_view, depth_view};
      VkFramebufferCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = render_pass, .attachmentCount = depth_format ? 2 : 1, .pAttachments = attachment_views,
         .width = w, .height = h, .layers = 1};
      CHECK(vkCreateFramebuffer(device, &fi, NULL, &framebuffer));
   }
   VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
   VkSampler sampler;
   CHECK(vkCreateSampler(device, &sci, NULL, &sampler));
   FILE *file = fopen(argv[2], "rb");
   if (!file || fseek(file, 0, SEEK_END)) return 1;
   long code_size = ftell(file);
   if (code_size < 20 || code_size > 1048576 || code_size % 4) return 1;
   rewind(file);
   uint32_t *code = malloc(code_size);
   if (!code || fread(code, 1, code_size, file) != (size_t)code_size) return 1;
   fclose(file);
   VkShaderModuleCreateInfo smi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code_size, .pCode = code};
   VkShaderModule shader;
   CHECK(vkCreateShaderModule(device, &smi, NULL, &shader));
   free(code);
   VkDescriptorSetLayoutBinding bindings[10] = {0};
   for (uint32_t i = 0; i < binding_base; i++)
      bindings[i] = (VkDescriptorSetLayoutBinding){.binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
   bindings[binding_base] = (VkDescriptorSetLayoutBinding){.binding = binding_base,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
   bindings[binding_base + 1] = (VkDescriptorSetLayoutBinding){.binding = binding_base + 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo sli = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = binding_base + 2, .pBindings = bindings};
   VkDescriptorSetLayout set_layout;
   CHECK(vkCreateDescriptorSetLayout(device, &sli, NULL, &set_layout));
   VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
   VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &set_layout,
      .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
   VkPipelineLayout layout;
   CHECK(vkCreatePipelineLayout(device, &pli, NULL, &layout));
   VkComputePipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"}, .layout = layout};
   VkPipeline pipeline;
   CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pi, NULL, &pipeline));
   VkPipeline render_pipeline = VK_NULL_HANDLE;
   VkShaderModule graphics_shaders[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
   if (graphics) {
      for (unsigned i = 0; i < 2; i++) {
         file = fopen(argv[7 + i], "rb");
         if (!file || fseek(file, 0, SEEK_END)) return 1;
         code_size = ftell(file);
         if (code_size < 20 || code_size > 1048576 || code_size % 4) return 1;
         rewind(file);
         code = malloc(code_size);
         if (!code || fread(code, 1, code_size, file) != (size_t)code_size) return 1;
         fclose(file);
         smi.codeSize = code_size;
         smi.pCode = code;
         CHECK(vkCreateShaderModule(device, &smi, NULL, &graphics_shaders[i]));
         free(code);
      }
      VkPipelineShaderStageCreateInfo stages[2] = {
         {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = graphics_shaders[0], .pName = "main"},
         {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = graphics_shaders[1], .pName = "main"}};
      VkPipelineVertexInputStateCreateInfo vertex = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
      VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
      VkViewport viewport = {0, 0, w, h, 0, 1};
      VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
         .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1};
      VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
         .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1};
      VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
         .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
      VkPipelineColorBlendAttachmentState ba = {.blendEnable = blend_enabled,
         .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
         .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
         .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 15};
      if (dual_source) {
         ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA;
         ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
         ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
      }
      VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
         .attachmentCount = 1, .pAttachments = &ba};
      VkDynamicState state = VK_DYNAMIC_STATE_SCISSOR;
      VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
         .dynamicStateCount = 1, .pDynamicStates = &state};
      VkPipelineDepthStencilStateCreateInfo depth_state = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
         .depthTestEnable = depth_format != VK_FORMAT_UNDEFINED,
         .depthWriteEnable = depth_format != VK_FORMAT_UNDEFINED,
         .depthCompareOp = reverse_depth ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_LESS};
      VkGraphicsPipelineCreateInfo gpi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
         .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex, .pInputAssemblyState = &assembly,
         .pViewportState = &vp, .pRasterizationState = &raster, .pMultisampleState = &samples,
         .pColorBlendState = &blend, .pDepthStencilState = &depth_state,
         .pDynamicState = &dynamic, .layout = layout, .renderPass = render_pass};
      CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, NULL, &render_pipeline));
   }
   VkDescriptorPoolSize pool_sizes[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, binding_base}};
   VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1, .poolSizeCount = binding_base ? 3 : 2, .pPoolSizes = pool_sizes};
   VkDescriptorPool descriptor_pool;
   CHECK(vkCreateDescriptorPool(device, &dpi, NULL, &descriptor_pool));
   VkDescriptorSetAllocateInfo sai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &set_layout};
   VkDescriptorSet set;
   CHECK(vkAllocateDescriptorSets(device, &sai, &set));
   VkDescriptorImageInfo image_info = {sampler, view, VK_IMAGE_LAYOUT_GENERAL};
   VkDescriptorBufferInfo buffer_info = {buffers[2], 0, sizes[2]};
   VkWriteDescriptorSet writes[10] = {0};
   VkDescriptorBufferInfo uniform_info = {binding_base ? buffers[4] : VK_NULL_HANDLE, 0, 16};
   for (uint32_t i = 0; i < binding_base; i++)
      writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set, .dstBinding = i, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &uniform_info};
   writes[binding_base] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = binding_base,
       .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .pImageInfo = &image_info};
   writes[binding_base + 1] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = binding_base + 1,
       .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pBufferInfo = &buffer_info};
   vkUpdateDescriptorSets(device, binding_base + 2, writes, 0, NULL);
   VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
   VkCommandPool command_pool;
   CHECK(vkCreateCommandPool(device, &cpi, NULL, &command_pool));
   VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer command;
   CHECK(vkAllocateCommandBuffers(device, &cai, &command));
   uint32_t *expected = malloc(pixels * 4);
   if (!expected) return 1;
   uint64_t raw_bad = 0, sample_bad = 0, render_bad = 0, depth_bad = 0, stencil_bad = 0;
   FILE *render_dump = NULL;
   FILE *raw_dump = NULL;
   FILE *image_dump = NULL;
   if (getenv("MALI_G2_TILES_DUMP_IMAGE")) {
      image_dump = fopen(getenv("MALI_G2_TILES_DUMP_IMAGE"), "wb");
      if (!image_dump) return 1;
      printf("TILES_IMAGE_ALLOCATION bytes=%llu\n", (unsigned long long)image_allocation_size);
   }
   if (getenv("MALI_G2_TILES_DUMP_RAW")) {
      raw_dump = fopen(getenv("MALI_G2_TILES_DUMP_RAW"), "wb");
      if (!raw_dump) return 1;
   }
   if (graphics && getenv("MALI_G2_TILES_DUMP_RENDER")) {
      render_dump = fopen(getenv("MALI_G2_TILES_DUMP_RENDER"), "wb");
      if (!render_dump) return 1;
   }
   VkQueryPool timestamp_query = VK_NULL_HANDLE;
   double *compute_times = NULL, *render_times = NULL;
   float timestamp_period = 0;
   if (timing) {
      compute_times = calloc(frames, sizeof(*compute_times));
      render_times = calloc(frames, sizeof(*render_times));
      if (!compute_times || !render_times) return 1;
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(physical, &properties);
      timestamp_period = properties.limits.timestampPeriod;
      VkQueryPoolCreateInfo q = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
         .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = graphics ? 4 : 2};
      CHECK(vkCreateQueryPool(device, &q, NULL, &timestamp_query));
   }
   for (uint32_t frame = 0; frame < frames; frame++) {
      if (binding_base) {
         float uniform[4] = {64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 0.5f};
         memcpy(mapped[4], uniform, sizeof(uniform));
      }
      if (compressed) {
         memset(mapped[0], 0, sizes[0]);
         for (uint32_t by = 0; by < blocks_h; by++) {
            for (uint32_t bx = 0; bx < blocks_w; bx++) {
               uint32_t c = block_color(format, bx, by, frame);
               uint8_t *p = (uint8_t *)mapped[0] + ((size_t)by * blocks_w + bx) * block_bytes;
               if (format == VK_FORMAT_ASTC_4x4_UNORM_BLOCK) {
                  uint64_t header = UINT64_C(0xfffffffffffffdfc);
                  memcpy(p, &header, 8);
                  for (unsigned channel = 0; channel < 4; channel++) {
                     uint16_t v = ((c >> (channel * 8)) & 255) * 257;
                     memcpy(p + 8 + channel * 2, &v, 2);
                  }
               } else if (format == VK_FORMAT_BC7_UNORM_BLOCK) {
                  unsigned offset = 0;
                  put_bits(p, &offset, 7, 64);
                  for (unsigned channel = 0; channel < 4; channel++) {
                     uint32_t v = (c >> (channel * 8 + 1)) & 127;
                     put_bits(p, &offset, 7, v);
                     put_bits(p, &offset, 7, v);
                  }
               } else {
                  uint16_t endpoint = ((c & 255) >> 3) << 11 |
                     ((c >> 8 & 255) >> 2) << 5 | ((c >> 16 & 255) >> 3);
                  if (format == VK_FORMAT_BC3_UNORM_BLOCK) {
                     p[0] = c >> 24;
                     p += 8;
                  }
                  memcpy(p, &endpoint, 2);
               }
            }
         }
      }
      for (size_t i = 0; i < pixels; i++) {
         uint32_t c = pattern(i % w, i / w, frame);
         if (wide) {
            for (unsigned channel = 0; channel < 4; channel++) {
               uint32_t v = c >> (channel * 8) & 255;
               if (format == VK_FORMAT_R16G16B16A16_UNORM) {
                  ((uint16_t *)mapped[0])[i * 4 + channel] = v * 257;
               } else if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
                  _Float16 f = (float)v / 255.0f;
                  memcpy((uint8_t *)mapped[0] + i * bpp + channel * 2, &f, 2);
               } else {
                  ((float *)mapped[0])[i * 4 + channel] = (float)v / 255.0f;
               }
            }
         } else if (compressed) {
            c = block_color(format, (i % w) / 4, (i / w) / 4, frame);
         } else if (format == VK_FORMAT_R8_UNORM) {
            ((uint8_t *)mapped[0])[i] = c & 255;
            c = (c & 255) | 0xff000000;
         } else if (format == VK_FORMAT_R8G8_UNORM) {
            ((uint16_t *)mapped[0])[i] = c & 65535;
            c = (c & 65535) | 0xff000000;
         } else if (format == VK_FORMAT_R5G6B5_UNORM_PACK16) {
            uint16_t v = ((c & 255) >> 3) << 11 | (((c >> 8) & 255) >> 2) << 5 |
               (((c >> 16) & 255) >> 3);
            ((uint16_t *)mapped[0])[i] = v;
            c = (((v >> 11) & 31) * 255 + 15) / 31 |
                ((((v >> 5) & 63) * 255 + 31) / 63) << 8 |
                (((v & 31) * 255 + 15) / 31) << 16 | 0xff000000;
         } else if (format == VK_FORMAT_A1R5G5B5_UNORM_PACK16) {
            uint16_t v = ((c & 255) >> 3) << 10 | (((c >> 8) & 255) >> 3) << 5 |
               (((c >> 16) & 255) >> 3) | (c >> 31) << 15;
            ((uint16_t *)mapped[0])[i] = v;
            c = (((v >> 10) & 31) * 255 + 15) / 31 |
                ((((v >> 5) & 31) * 255 + 15) / 31) << 8 |
                (((v & 31) * 255 + 15) / 31) << 16 | (v >> 15 ? 0xff000000 : 0);
         } else if (bpp == 2) {
            uint16_t v = ((c & 255) >> 3) << 11 | (((c >> 8) & 255) >> 3) << 6 |
               (((c >> 16) & 255) >> 3) << 1 | (c >> 31);
            ((uint16_t *)mapped[0])[i] = v;
            c = (((v >> 11) & 31) * 255 + 15) / 31 |
                ((((v >> 6) & 31) * 255 + 15) / 31) << 8 |
                ((((v >> 1) & 31) * 255 + 15) / 31) << 16 | (v & 1 ? 0xff000000 : 0);
         } else {
            ((uint32_t *)mapped[0])[i] = format == VK_FORMAT_B8G8R8A8_UNORM ?
               (c & 0xff00ff00) | (c & 255) << 16 | (c >> 16 & 255) : c;
         }
         expected[i] = alpha_r ? 0x00ffffff | (c & 255) << 24 : c;
      }
      memset(mapped[1], 0xa5, sizes[1]);
      memset(mapped[2], 0x5a, sizes[2]);
      if (graphics) memset(mapped[3], 0xa5, sizes[3]);
      CHECK(vkResetCommandBuffer(command, 0));
      VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      CHECK(vkBeginCommandBuffer(command, &begin));
      if (timing) vkCmdResetQueryPool(command, timestamp_query, 0, graphics ? 4 : 2);
      VkImageMemoryBarrier ib = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = frame ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT : 0,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout = frame ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
         .subresourceRange = vi.subresourceRange};
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
      VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1},
         .imageExtent = {w, h, 1}};
      uint32_t xs[3] = {0, (w / 2) & ~3u, w}, ys[3] = {0, (h / 2) & ~3u, h};
      int split = xs[1] && ys[1];
      if (split_upload && split) {
         VkBufferImageCopy parts[4];
         for (unsigned y = 0; y < 2; y++) for (unsigned x = 0; x < 2; x++) {
            VkBufferImageCopy *p = &parts[y * 2 + x];
            *p = region;
            p->bufferRowLength = compressed ? blocks_w * 4 : w;
            p->bufferImageHeight = compressed ? blocks_h * 4 : h;
            p->imageOffset = (VkOffset3D){xs[x], ys[y], 0};
            p->imageExtent = (VkExtent3D){xs[x + 1] - xs[x], ys[y + 1] - ys[y], 1};
            p->bufferOffset = compressed ? ((size_t)(ys[y] / 4) * blocks_w + xs[x] / 4) * block_bytes :
               ((size_t)ys[y] * w + xs[x]) * bpp;
         }
         vkCmdCopyBufferToImage(command, buffers[0], image, VK_IMAGE_LAYOUT_GENERAL, 4, parts);
      } else vkCmdCopyBufferToImage(command, buffers[0], image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
      ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
      ib.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         0, 0, NULL, 0, NULL, 1, &ib);
      if (image_copy) {
         VkImageMemoryBarrier cb = ib;
         cb.image = copy_image;
         cb.oldLayout = frame ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
         cb.srcAccessMask = frame ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT : 0;
         cb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &cb);
         VkImageCopy parts[4];
         unsigned count = split ? 4 : 1;
         for (unsigned n = 0; n < count; n++) {
            unsigned x = n % 2, y = n / 2;
            VkOffset3D off = split ? (VkOffset3D){xs[x], ys[y], 0} : (VkOffset3D){0, 0, 0};
            VkExtent3D extent = split ? (VkExtent3D){xs[x + 1] - xs[x], ys[y + 1] - ys[y], 1} : (VkExtent3D){w, h, 1};
            parts[n] = (VkImageCopy){.srcSubresource = region.imageSubresource,
               .srcOffset = off, .dstSubresource = region.imageSubresource, .dstOffset = off, .extent = extent};
         }
         if (via_image) {
            VkImageMemoryBarrier vb = cb;
            vb.image = via_image;
            vb.subresourceRange.baseMipLevel = 0;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &vb);
            VkImageCopy to_via = {
               .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1},
               .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1},
               .extent = {w, h, 1}};
            vkCmdCopyImage(command, image, VK_IMAGE_LAYOUT_GENERAL, via_image,
               VK_IMAGE_LAYOUT_GENERAL, 1, &to_via);
            vb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &vb);
            VkImageCopy from_via = {
               .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1},
               .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1},
               .extent = {(w + 3) / 4, (h + 3) / 4, 1}};
            vkCmdCopyImage(command, via_image, VK_IMAGE_LAYOUT_GENERAL, copy_image,
               VK_IMAGE_LAYOUT_GENERAL, 1, &from_via);
         } else {
            vkCmdCopyImage(command, image, VK_IMAGE_LAYOUT_GENERAL, copy_image, VK_IMAGE_LAYOUT_GENERAL, count, parts);
         }
         cb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
         cb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         cb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
         vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &cb);
      }
      vkCmdCopyImageToBuffer(command, sampled_image, VK_IMAGE_LAYOUT_GENERAL, buffers[1], 1, &region);
      if (timing) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timestamp_query, 0);
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
      uint32_t dims[2] = {w, h};
      vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dims), dims);
      vkCmdDispatch(command, (w + 7) / 8, (h + 7) / 8, 1);
      if (timing) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timestamp_query, 1);
      VkRect2D scissor = {{0, 0}, {w, h}};
      if (graphics) {
         if (scissor_enabled) {
            scissor.offset.x = (frame * 7 + 3) % ((w + 3) / 4);
            scissor.offset.y = (frame * 3 + 1) % ((h + 3) / 4);
            scissor.extent.width -= 2 * scissor.offset.x;
            scissor.extent.height -= 2 * scissor.offset.y;
         }
         VkImageMemoryBarrier rb = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = frame ? VK_ACCESS_TRANSFER_READ_BIT : 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = frame ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = render_image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
         vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &rb);
         if (depth_format) {
            VkImageMemoryBarrier db = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
               .srcAccessMask = frame ? (depth_readback ? VK_ACCESS_TRANSFER_READ_BIT :
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) : 0,
               .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               .oldLayout = frame ? (depth_readback ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL :
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) : VK_IMAGE_LAYOUT_UNDEFINED,
               .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
               .image = depth_image, .subresourceRange = {depth_aspects, 0, 1, 0, 1}};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
               0, 0, NULL, 0, NULL, 1, &db);
         }
         VkClearValue clears[2] = {{.color = {.float32 = {0, 0, 0, 0}}},
            {.depthStencil = {reverse_depth != reject_depth ? 0.0f : 1.0f, 0x5a}}};
         if (white_background) clears[0] = (VkClearValue){.color = {.float32 = {1, 1, 1, 1}}};
         if (timing) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timestamp_query, 2);
         VkRenderPassBeginInfo rbi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = render_pass, .framebuffer = framebuffer, .renderArea = {{0, 0}, {w, h}},
            .clearValueCount = depth_format ? 2 : 1, .pClearValues = clears};
         vkCmdBeginRenderPass(command, &rbi, VK_SUBPASS_CONTENTS_INLINE);
         vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pipeline);
         vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
         vkCmdSetScissor(command, 0, 1, &scissor);
         if (index_bits) vkCmdBindIndexBuffer(command, buffers[5], 0,
            index_bits == 16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
         if (!load_draw) {
            if (index_bits) vkCmdDrawIndexed(command, 3, 1, 7, vertex_offset, 0);
            else vkCmdDraw(command, 3, 1, 0, 0);
         }
         vkCmdEndRenderPass(command);
         if (load_enabled || load_draw) {
            VkMemoryBarrier preserve = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
               .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
            if (depth_format) {
               preserve.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
               preserve.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &preserve, 0, NULL, 0, NULL);
            rbi.renderPass = load_pass;
            rbi.clearValueCount = 0;
            if (partial_render) rbi.renderArea = scissor;
            vkCmdBeginRenderPass(command, &rbi, VK_SUBPASS_CONTENTS_INLINE);
            if (rebind_load) {
               vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pipeline);
               vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
               vkCmdSetScissor(command, 0, 1, &scissor);
            }
            if (load_draw) {
               if (index_bits) vkCmdDrawIndexed(command, 3, 1, 7, vertex_offset, 0);
               else vkCmdDraw(command, 3, 1, 0, 0);
            }
            vkCmdEndRenderPass(command);
         }
         if (timing) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timestamp_query, 3);
         rb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
         rb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
         rb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
         rb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
         vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &rb);
         VkBufferImageCopy render_region = region;
         render_region.imageSubresource.mipLevel = 0;
         render_region.imageSubresource.baseArrayLayer = 0;
         vkCmdCopyImageToBuffer(command, render_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffers[3], 1, &render_region);
         if (depth_readback) {
            VkImageMemoryBarrier db = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
               .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
               .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
               .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
               .image = depth_image,
               .subresourceRange = {depth_aspects, 0, 1, 0, 1},
            };
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &db);
            VkBufferImageCopy depth_region = {
               .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
               .imageExtent = {w, h, 1},
            };
            vkCmdCopyImageToBuffer(command, depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  buffers[6], 1, &depth_region);
            if (depth_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
               depth_region.bufferOffset = pixels * 4;
               depth_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
               vkCmdCopyImageToBuffer(command, depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     buffers[6], 1, &depth_region);
            }
            if (depth_roundtrip) {
               VkMemoryBarrier source_ready = {
                  .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                  .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
               };
               db.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
               db.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
               db.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
               db.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
               vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &source_ready, 0, NULL, 1, &db);
               VkClearDepthStencilValue poison = {0.125f, 0xc3};
               vkCmdClearDepthStencilImage(command, depth_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          &poison, 1, &db.subresourceRange);
               db.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
               db.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
               vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &db);
               VkBufferImageCopy copies[2] = {{
                  .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                  .imageExtent = {w, h, 1},
               }, {
                  .bufferOffset = pixels * 4,
                  .imageSubresource = {VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1},
                  .imageExtent = {w, h, 1},
               }};
               uint32_t count = depth_aspects & VK_IMAGE_ASPECT_STENCIL_BIT ? 2 : 1;
               vkCmdCopyBufferToImage(command, buffers[6], depth_image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, copies);
               db.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
               db.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
               vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &db);
               vkCmdCopyImageToBuffer(command, depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     buffers[7], count, copies);
            }
         }
      }
      VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
      CHECK(vkEndCommandBuffer(command));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1, .pCommandBuffers = &command};
      CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
      CHECK(vkQueueWaitIdle(queue));
      if (timing) {
         uint64_t stamps[4];
         CHECK(vkGetQueryPoolResults(device, timestamp_query, 0, graphics ? 4 : 2,
            sizeof(stamps), stamps, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
         if (stamps[1] < stamps[0] || (graphics && stamps[3] < stamps[2])) return 1;
         compute_times[frame] = (stamps[1] - stamps[0]) * (double)timestamp_period / 1000.0;
         if (graphics) render_times[frame] = (stamps[3] - stamps[2]) * (double)timestamp_period / 1000.0;
      }
      if (image_dump && fwrite(image_mapped, 1, image_allocation_size, image_dump) != image_allocation_size)
         return 1;
      if (raw_dump && (fwrite(mapped[0], 1, raw_size, raw_dump) != raw_size ||
                       fwrite(mapped[1], 1, raw_size, raw_dump) != raw_size)) return 1;
      if (render_dump && fwrite(mapped[3], 1, sizes[3], render_dump) != sizes[3]) return 1;
      if (compressed) {
         for (size_t i = 0; i < raw_size; i++) {
            if (((uint8_t *)mapped[0])[i] != ((uint8_t *)mapped[1])[i]) {
               if (!raw_bad) printf("RAW_MISMATCH frame=%u byte=%zu\n", frame, i);
               raw_bad++;
            }
         }
      }
      for (size_t i = 0; i < pixels; i++) {
         if (!compressed && memcmp((uint8_t *)mapped[0] + i * bpp, (uint8_t *)mapped[1] + i * bpp, bpp)) {
            if (!raw_bad) printf("RAW_MISMATCH frame=%u x=%zu y=%zu\n", frame, i % w, i / w);
            raw_bad++;
         }
         if (((uint32_t *)mapped[2])[i] != expected[i]) {
            if (!sample_bad) printf("SAMPLE_MISMATCH frame=%u x=%zu y=%zu got=%08x expected=%08x\n",
               frame, i % w, i / w, ((uint32_t *)mapped[2])[i], expected[i]);
            sample_bad++;
         }
         if (graphics) {
            uint32_t color = expected[i], wanted = white_background ? 0xffffffff : 0;
            if (tint_enabled) color = (((color & 255) + 64 + 1) / 2) |
               ((((color >> 8 & 255) + 128 + 1) / 2) << 8) |
               ((((color >> 16 & 255) + 192 + 1) / 2) << 16) | (color & 0xff000000);
            uint32_t x = i % w, y = i / w;
            if (x >= (uint32_t)scissor.offset.x && x < (uint32_t)scissor.offset.x + scissor.extent.width &&
                y >= (uint32_t)scissor.offset.y && y < (uint32_t)scissor.offset.y + scissor.extent.height) {
               wanted = color;
               if (blend_enabled) {
                  uint32_t a = color >> 24;
                  wanted = ((color & 255) * a + 127) / 255 |
                     ((((color >> 8) & 255) * a + 127) / 255) << 8 |
                     ((((color >> 16) & 255) * a + 127) / 255) << 16 | a << 24;
                  if (white_background) wanted = (((color & 255) * a + 255 * (255 - a) + 127) / 255) |
                     ((((color >> 8 & 255) * a + 255 * (255 - a) + 127) / 255) << 8) |
                     ((((color >> 16 & 255) * a + 255 * (255 - a) + 127) / 255) << 16) | 0xff000000;
               }
            }
            if (discard_enabled && (color >> 24) < 128) wanted = white_background ? 0xffffffff : 0;
            if (depth_format && reject_depth) wanted = white_background ? 0xffffffff : 0;
            if (render_format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)
               wanted = (wanted & 0x00ffffff) | (((wanted >> 24) * 3 + 127) / 255) * 85 << 24;
            if (render_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) wanted |= 0xff000000;
            uint32_t got = read_render_pixel(mapped[3], i, render_format);
            int mismatch = 0;
            int tolerance = render_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ? 2 : blend_enabled;
            for (unsigned channel = 0; channel < 4; channel++)
               mismatch |= abs((int)(got >> (channel * 8) & 255) - (int)(wanted >> (channel * 8) & 255)) > tolerance;
            if (mismatch) {
               if (!render_bad) printf("RENDER_MISMATCH frame=%u x=%u y=%u got=%08x expected=%08x\n", frame, x, y, got, wanted);
               render_bad++;
            }
            if (depth_readback) {
               const void *depth_pixels = mapped[depth_roundtrip ? 7 : 6];
               float wanted_depth = reverse_depth != reject_depth ? 0.0f : 1.0f;
               int covered = x >= (uint32_t)scissor.offset.x &&
                  x < (uint32_t)scissor.offset.x + scissor.extent.width &&
                  y >= (uint32_t)scissor.offset.y &&
                  y < (uint32_t)scissor.offset.y + scissor.extent.height;
               if (covered && !reject_depth && !(discard_enabled && (color >> 24) < 128))
                  wanted_depth = 0.5f;
               float got_depth;
               int depth_mismatch;
               if (depth_format == VK_FORMAT_D16_UNORM) {
                  uint32_t actual = ((const uint16_t *)depth_pixels)[i];
                  uint32_t target = (uint32_t)(wanted_depth * 65535.0f + 0.5f);
                  got_depth = (float)actual / 65535.0f;
                  depth_mismatch = abs((int)actual - (int)target) > 1;
               } else if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT) {
                  uint32_t actual = ((const uint32_t *)depth_pixels)[i] & 0xffffff;
                  uint32_t target = (uint32_t)(wanted_depth * 16777215.0f + 0.5f);
                  got_depth = (float)actual / 16777215.0f;
                  depth_mismatch = abs((int)actual - (int)target) > 1;
               } else {
                  got_depth = ((const float *)depth_pixels)[i];
                  float delta = got_depth - wanted_depth;
                  depth_mismatch = delta != delta || delta < -0.000001f || delta > 0.000001f;
               }
               if (depth_mismatch) {
                  if (!depth_bad) printf("DEPTH_MISMATCH frame=%u x=%u y=%u got=%f expected=%f\n",
                                         frame, x, y, got_depth, wanted_depth);
                  depth_bad++;
               }
               if (depth_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
                  uint8_t stencil = ((const uint8_t *)depth_pixels)[pixels * 4 + i];
                  if (stencil != 0x5a) {
                     if (!stencil_bad) printf("STENCIL_MISMATCH frame=%u x=%u y=%u got=%02x expected=5a\n",
                                             frame, x, y, stencil);
                     stencil_bad++;
                  }
               }
            }
         }
      }
   }
   printf("TILES_%s %ux%u format=%u frames=%u mip=%u layer=%u raw_bad=%llu sample_bad=%llu render=%d rt=%u blend=%d dual=%d discard=%d scissor=%d load=%d load_draw=%d partial=%d white=%d binding=%u image_copy=%d split_upload=%d alpha_r=%d depth=%u reverse=%d reject=%d indices=%u vertex_offset=%d render_bad=%llu depth_bad=%llu stencil_bad=%llu\n",
      raw_bad || sample_bad || render_bad || depth_bad || stencil_bad ? "FAIL" : "PASS", w, h, format, frames, mip, layer,
      (unsigned long long)raw_bad, (unsigned long long)sample_bad, graphics, render_format, blend_enabled, dual_source, discard_enabled, scissor_enabled, load_enabled, load_draw, partial_render, white_background, binding_base, image_copy, split_upload, alpha_r,
      depth_format, reverse_depth, reject_depth, index_bits, vertex_offset,
      (unsigned long long)render_bad, (unsigned long long)depth_bad, (unsigned long long)stencil_bad);
   if (timing) {
      qsort(compute_times, frames, sizeof(*compute_times), compare_double);
      qsort(render_times, frames, sizeof(*render_times), compare_double);
      printf("TILES_TIMING frames=%u compute_median_us=%.3f render_median_us=%.3f\n",
             frames, compute_times[frames / 2], render_times[frames / 2]);
      vkDestroyQueryPool(device, timestamp_query, NULL);
      free(compute_times);
      free(render_times);
   }
   free(expected);
   if (audit) {
      unsigned signals=0;
      for (unsigned i=0;i<5;i++) {
         struct sigaction after;
         sigaction(audit_signals[i],NULL,&after);
         signals+=after.sa_sigaction!=before[i].sa_sigaction;
      }
      printf("DIAG_AUDIT sockets=%u signals=%u\n",udp_sockets()-sockets_before,signals);
   }
   if (render_dump) fclose(render_dump);
   if (raw_dump) fclose(raw_dump);
   if (image_dump) fclose(image_dump);
   vkDestroyCommandPool(device, command_pool, NULL);
   vkDestroyDescriptorPool(device, descriptor_pool, NULL);
   vkDestroyPipeline(device, pipeline, NULL);
   vkDestroyPipelineLayout(device, layout, NULL);
   vkDestroyDescriptorSetLayout(device, set_layout, NULL);
   vkDestroyShaderModule(device, shader, NULL);
   if (graphics) {
      vkDestroyPipeline(device, render_pipeline, NULL);
      for (unsigned i = 0; i < 2; i++) vkDestroyShaderModule(device, graphics_shaders[i], NULL);
      vkDestroyFramebuffer(device, framebuffer, NULL);
      vkDestroyRenderPass(device, render_pass, NULL);
      vkDestroyRenderPass(device, load_pass, NULL);
      vkDestroyImageView(device, render_view, NULL);
      vkDestroyImage(device, render_image, NULL);
      vkFreeMemory(device, render_memory, NULL);
      if (depth_format) {
         vkDestroyImageView(device, depth_view, NULL);
         vkDestroyImage(device, depth_image, NULL);
         vkFreeMemory(device, depth_memory, NULL);
      }
   }
   vkDestroySampler(device, sampler, NULL);
   vkDestroyImageView(device, view, NULL);
   vkDestroyImage(device, image, NULL);
   if (image_mapped) vkUnmapMemory(device, image_memory);
   vkFreeMemory(device, image_memory, NULL);
   if (image_copy) {
      vkDestroyImage(device, copy_image, NULL);
      vkFreeMemory(device, copy_memory, NULL);
   }
   if (via_image) {
      vkDestroyImage(device, via_image, NULL);
      vkFreeMemory(device, via_memory, NULL);
   }
   for (unsigned i = 0; i < buffer_count; i++) {
      vkUnmapMemory(device, allocations[i]);
      vkDestroyBuffer(device, buffers[i], NULL);
      vkFreeMemory(device, allocations[i], NULL);
   }
   vkDestroyDevice(device, NULL);
   vkDestroyInstance(instance, NULL);
   dlclose(library);
   return raw_bad || sample_bad || render_bad || depth_bad || stencil_bad ? 1 : 0;
}
