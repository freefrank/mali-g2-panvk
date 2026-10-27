/*
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_MODEL_H
#define PAN_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "util/macros.h"

/** Implementation-defined tiler features */
struct pan_tiler_features {
   /** Number of bytes per tiler bin */
   unsigned bin_size;

   /** Maximum number of levels that may be simultaneously enabled.
    * Invariant: bitcount(hierarchy_mask) <= max_levels */
   unsigned max_levels;
};

static inline bool
pan_gpu_id_is_64bit(uint64_t gpu_id)
{
   return ((gpu_id >> 28) & 0xf) == 0xf;
}

static inline uint32_t
pan_gpu_id_field(uint64_t gpu_id, unsigned shift32, unsigned shift64,
                 uint32_t mask32)
{
   if (pan_gpu_id_is_64bit(gpu_id))
      return (gpu_id >> shift64) & 0xff;

   return (gpu_id >> shift32) & mask32;
}

#define PAN_ARCH_MAJOR(x)    pan_gpu_id_field((x), 28, 56, 0xf)
#define PAN_ARCH_MINOR(x)    pan_gpu_id_field((x), 24, 48, 0xf)
#define PAN_ARCH_REV(x)      pan_gpu_id_field((x), 20, 40, 0xf)
#define PAN_PRODUCT_MAJOR(x) pan_gpu_id_field((x), 16, 32, 0xf)

#define PAN_VERSION_MAJOR(x)  pan_gpu_id_field((x), 12, 16, 0xf)
#define PAN_VERSION_MINOR(x)  pan_gpu_id_field((x), 4, 8, 0xff)
#define PAN_VERSION_STATUS(x) pan_gpu_id_field((x), 0, 0, 0xf)

/* GPU product id for Midgard */
#define MIDGARD_PROD_ID(x) (((x) & BITFIELD_RANGE(16, 16)) >> 16)

/* GPU product id since Bifrost. Assume 8 bits per field which ensures the
 * prod_id is always greater than Midgard's. */
#define PAN_PROD_ID(arch_major, arch_minor, prod_major)                        \
   (((arch_major) << 16) | ((arch_minor) << 8) | (prod_major))

/* GPU revision (rXpY) */
#define PAN_REV(ver_major, ver_minor) (((ver_major) << 8) | ((ver_minor)))

struct pan_model {
   /* GPU product ID */
   uint32_t gpu_prod_id;

   /* GPU variant. */
   uint32_t gpu_variant;

   /* minimum core count */
   unsigned min_core_count;

   /* required GPU features. */
   uint32_t required_gpu_features;

   /* Marketing name for the GPU, used as the GL_RENDERER */
   const char *name;

   /* Set of associated performance counters */
   const char *performance_counters;

   /* Minimum GPU revision required for anisotropic filtering. ~0 and 0
    * means "no revisions support anisotropy" and "all revisions support
    * anistropy" respectively -- so checking for anisotropy is simply
    * comparing the reivsion.
    */
   uint32_t min_rev_anisotropic;

   struct {
      /* Default tilebuffer size in bytes for the model. */
      uint32_t color_size;

      /* Default tilebuffer depth size in bytes for the model. */
      uint32_t z_size;
   } tilebuffer;

   /* Maximum number of pixels, texels, and FMA ops, per clock per shader
    * core, or 0 if it can't be determined for the given GPU. */
   struct {
      uint32_t pixel;
      uint32_t texel;
      uint32_t varying;
      uint32_t cvt;
      uint32_t fma;
      uint32_t sfu;
   } rates;

   struct {
      /* The GPU lacks the capability for hierarchical tiling, without
       * an "Advanced Tiling Unit", instead requiring a single bin
       * size for the entire framebuffer be selected by the driver
       */
      bool no_hierarchical_tiling;
      bool max_4x_msaa;
   } quirks;
};

const struct pan_model *pan_get_model(uint64_t gpu_id, uint32_t gpu_variant,
                                      unsigned core_count, uint64_t gpu_features);

/* Returns the architecture version given a GPU ID, either from a table for
 * old-style Midgard versions or directly for new-style Bifrost/Valhall
 * versions */

static inline unsigned
pan_arch(uint64_t gpu_id)
{
   if (pan_gpu_id_is_64bit(gpu_id))
      return PAN_ARCH_MAJOR(gpu_id);

   switch (MIDGARD_PROD_ID(gpu_id)) {
   case 0x600:
   case 0x620:
   case 0x720:
      return 4;
   case 0x750:
   case 0x820:
   case 0x830:
   case 0x860:
   case 0x880:
      return 5;
   default:
      return PAN_ARCH_MAJOR(gpu_id);
   }
}

static inline uint32_t
pan_prod_id(uint64_t gpu_id)
{
   unsigned arch = pan_arch(gpu_id);
   if (arch < 6)
      return MIDGARD_PROD_ID(gpu_id);
   return PAN_PROD_ID(PAN_ARCH_MAJOR(gpu_id), PAN_ARCH_MINOR(gpu_id),
                      PAN_PRODUCT_MAJOR(gpu_id));
}

static inline uint32_t
pan_rev(uint64_t gpu_id)
{
   return PAN_REV(PAN_VERSION_MAJOR(gpu_id), PAN_VERSION_MINOR(gpu_id));
}

static inline int
pan_model_device_name(char *buf, size_t size, const struct pan_model *model,
                      unsigned core_count, const char *suffix)
{
   return snprintf(buf, size, "%s MC%u%s", model->name, core_count, suffix);
}

#endif
