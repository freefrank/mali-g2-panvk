#pragma once

#include <stdint.h>
#include <string.h>

/* ---- GPU property stream ------------------------------------------------------------------
 *
 * GET_GPUPROPS returns a packed run of (u32 header, value) pairs where header = (id << 2) | w,
 * w selecting a 1/2/4/8-byte value. It returns the BYTE COUNT, not zero, so only a negative
 * return is a failure -- treating non-zero as an error once made a successful 723-byte read
 * report a stale ENOENT.
 *
 * The ids below are anchored to values known independently for the reference part, rather than
 * copied from a table that proved wrong: PRODUCT_ID reads 0x7211 which is exactly the deviceID
 * the vendor's own Vulkan reports (0x72110000); SHADER_PRESENT reads 0x770077, whose popcount is
 * 12, matching a Mali-G76 MP12; and MMU_FEATURES reads 0x2830, i.e. 48 VA bits and 40 PA bits.
 * Three independent confirmations, so the surrounding ids can be trusted by position. */

enum kbase_gpuprop {
   KBASE_GPUPROP_PRODUCT_ID = 1,
   KBASE_GPUPROP_VERSION_STATUS = 2,
   KBASE_GPUPROP_MINOR_REVISION = 3,
   KBASE_GPUPROP_MAJOR_REVISION = 4,
   KBASE_GPUPROP_TEXTURE_FEATURES_0 = 9,
   KBASE_GPUPROP_TEXTURE_FEATURES_1 = 10,
   KBASE_GPUPROP_TEXTURE_FEATURES_2 = 11,
   KBASE_GPUPROP_L2_LOG2_LINE_SIZE = 13,
   KBASE_GPUPROP_L2_LOG2_CACHE_SIZE = 14,
   KBASE_GPUPROP_L2_NUM_L2_SLICES = 15,
   KBASE_GPUPROP_TILER_BIN_SIZE_BYTES = 16,
   KBASE_GPUPROP_TILER_MAX_ACTIVE_LEVELS = 17,
   KBASE_GPUPROP_MAX_THREADS = 18,
   KBASE_GPUPROP_MAX_WORKGROUP_SIZE = 19,
   KBASE_GPUPROP_MAX_BARRIER_SIZE = 20,
   KBASE_GPUPROP_MAX_REGISTERS = 21,
   KBASE_GPUPROP_MAX_TASK_QUEUE = 22,
   KBASE_GPUPROP_IMPL_TECH = 24,
   KBASE_GPUPROP_RAW_SHADER_PRESENT = 25,
   KBASE_GPUPROP_RAW_TILER_PRESENT = 26,
   KBASE_GPUPROP_RAW_L2_PRESENT = 27,
   KBASE_GPUPROP_RAW_STACK_PRESENT = 28,
   KBASE_GPUPROP_RAW_L2_FEATURES = 29,
   KBASE_GPUPROP_RAW_CORE_FEATURES = 30,
   KBASE_GPUPROP_RAW_MEM_FEATURES = 31,
   KBASE_GPUPROP_RAW_MMU_FEATURES = 32,
   KBASE_GPUPROP_RAW_AS_PRESENT = 33,
   KBASE_GPUPROP_RAW_JS_PRESENT = 34,
   /* 51, NOT 84. 84 is TLS_ALLOC, which is also present in the stream, so reading it did not
    * fall back to the default -- it fed Mesa 0x300 where the real value is 0x809. Decoded, that
    * is a tiler with a 1-BYTE bin and 3 hierarchy levels instead of 512 bytes and 8, i.e. a
    * tiler with nowhere to put a primitive. Every job still completed without a fault; the
    * polygon list was simply empty. Confirmed against the bv_r32p1 UAPI header, which is the
    * DDK this device runs. */
   KBASE_GPUPROP_RAW_TILER_FEATURES = 51,
   KBASE_GPUPROP_RAW_GPU_ID = 55,
   KBASE_GPUPROP_RAW_THREAD_FEATURES = 59,
   KBASE_GPUPROP_TLS_ALLOC = 84,
};

static inline uint64_t
kbase_gpuprop_get(const uint8_t *buf, unsigned size, unsigned want, uint64_t dflt)
{
   for (unsigned off = 0; off + 4 <= size;) {
      uint32_t hdr;
      memcpy(&hdr, buf + off, 4);
      off += 4;

      const unsigned id = hdr >> 2, type = hdr & 3;
      const unsigned w = type == 0 ? 1 : type == 1 ? 2 : type == 2 ? 4 : 8;
      if (off + w > size)
         break;

      if (id == want) {
         uint64_t v = 0;
         memcpy(&v, buf + off, w);
         return v;
      }
      off += w;
   }
   return dflt;
}

static inline uint64_t
kbase_gpuprops_gpu_id(const uint8_t *buf, unsigned size)
{
   uint64_t raw_id = kbase_gpuprop_get(buf, size, KBASE_GPUPROP_RAW_GPU_ID, 0);
   if (raw_id)
      return raw_id;

   uint64_t product = kbase_gpuprop_get(buf, size, KBASE_GPUPROP_PRODUCT_ID, 0);
   if (product > UINT16_MAX)
      return 0;

   uint64_t major = kbase_gpuprop_get(buf, size, KBASE_GPUPROP_MAJOR_REVISION, 0);
   uint64_t minor = kbase_gpuprop_get(buf, size, KBASE_GPUPROP_MINOR_REVISION, 0);
   uint64_t status = kbase_gpuprop_get(buf, size, KBASE_GPUPROP_VERSION_STATUS, 0);
   return (product << 16) | ((major & 0xf) << 12) |
          ((minor & 0xff) << 4) | (status & 0xf);
}
