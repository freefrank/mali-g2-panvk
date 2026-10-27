#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "panfrost/model/pan_model.h"
#include "panfrost/lib/kmod/kbase_gpuprops.h"

static unsigned checks;
static unsigned failures;

static void
expect(const char *name, uint64_t actual, uint64_t wanted)
{
   checks++;
   if (actual != wanted) {
      fprintf(stderr, "FAIL %s: got %#" PRIx64 ", expected %#" PRIx64 "\n",
              name, actual, wanted);
      failures++;
   }
}

static unsigned
append_prop(uint8_t *buffer, unsigned offset, unsigned id, unsigned type,
             uint64_t value)
{
   uint32_t header = (id << 2) | type;
   unsigned width = 1u << type;
   memcpy(buffer + offset, &header, sizeof(header));
   memcpy(buffer + offset + sizeof(header), &value, width);
   return offset + sizeof(header) + width;
}

static void
test_models(void)
{
   const struct {
      uint64_t id;
      unsigned arch;
      unsigned minor;
      unsigned arch_rev;
      unsigned product;
      unsigned major_rev;
      unsigned minor_rev;
      unsigned status;
      unsigned variant;
      const char *model;
   } cases[] = {
      {0x06000000, 4, 6, 0, 0, 0, 0, 0, 0, "Mali-T600"},
      {0x07500000, 5, 7, 5, 0, 0, 0, 0, 0, "Mali-T760"},
      {0x72111030, 7, 2, 1, 1, 1, 3, 0, 0, "Mali-G76"},
      {0x74021000, 7, 4, 0, 2, 1, 0, 0, 0, "Mali-G52 r1"},
      {0x90930010, 9, 0, 9, 3, 0, 1, 0, 0, "Mali-G57"},
      {0x90001000, 9, 0, 0, 0, 1, 0, 0, 0, "Mali-G77"},
      {0x90921000, 9, 0, 9, 2, 1, 0, 0, 0, "Mali-G78"},
      {0xa8070000, 10, 8, 0, 7, 0, 0, 0, 0, "Mali-G610"},
      {0xb8a31030, 11, 8, 10, 3, 1, 3, 0, 4, "Mali-G615"},
      {0xc8001000, 12, 8, 0, 0, 1, 0, 0, 4, "Mali-G720"},
      {0xd8000000, 13, 8, 0, 0, 0, 0, 0, 4, "Mali-G725"},
      {0xe8000000, 14, 8, 0, 0, 0, 0, 0, 4, "Mali-G1-Ultra"},
      {UINT64_C(0x0f080600f0000103), 15, 8, 6, 0, 0, 1, 3, 4, NULL},
      {UINT64_C(0x12345678f09abcde), 18, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 4, NULL},
   };

   for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i) {
      expect("architecture", pan_arch(cases[i].id), cases[i].arch);
      expect("architecture minor", PAN_ARCH_MINOR(cases[i].id), cases[i].minor);
      expect("architecture revision", PAN_ARCH_REV(cases[i].id), cases[i].arch_rev);
      expect("product major", PAN_PRODUCT_MAJOR(cases[i].id), cases[i].product);
      expect("revision major", PAN_VERSION_MAJOR(cases[i].id), cases[i].major_rev);
      expect("revision minor", PAN_VERSION_MINOR(cases[i].id), cases[i].minor_rev);
      expect("revision status", PAN_VERSION_STATUS(cases[i].id), cases[i].status);
      expect("combined revision", pan_rev(cases[i].id),
             PAN_REV(cases[i].major_rev, cases[i].minor_rev));
      const struct pan_model *model =
         pan_get_model(cases[i].id, cases[i].variant, 16, 0);
      if (cases[i].model) {
         expect("known model exists", model != NULL, true);
         if (model)
            expect("known model name", strcmp(model->name, cases[i].model), 0);
      } else {
         expect("unknown architecture has no false model", model == NULL, true);
      }
   }
   expect("G2 product", pan_prod_id(UINT64_C(0x0f080600f0000103)), 0x0f0800);
   expect("wide product fields", pan_prod_id(UINT64_C(0x12345678f09abcde)), 0x123478);
}

static void
test_property_streams(void)
{
   uint8_t buffer[128] = {0};
   unsigned n = 0;
   n = append_prop(buffer, n, KBASE_GPUPROP_PRODUCT_ID, 2, 0x7211);
   n = append_prop(buffer, n, KBASE_GPUPROP_MAJOR_REVISION, 0, 1);
   n = append_prop(buffer, n, KBASE_GPUPROP_MINOR_REVISION, 1, 3);
   n = append_prop(buffer, n, KBASE_GPUPROP_VERSION_STATUS, 0, 0);
   expect("legacy fallback", kbase_gpuprops_gpu_id(buffer, n), 0x72111030);
   n = append_prop(buffer, n, KBASE_GPUPROP_RAW_GPU_ID, 2, 0x72112040);
   expect("raw 32-bit ID takes precedence", kbase_gpuprops_gpu_id(buffer, n), 0x72112040);

   n = 0;
   n = append_prop(buffer, n, KBASE_GPUPROP_PRODUCT_ID, 2, 0x0f080600);
   expect("extended product without raw ID rejected", kbase_gpuprops_gpu_id(buffer, n), 0);
   n = append_prop(buffer, n, KBASE_GPUPROP_RAW_GPU_ID, 3,
                   UINT64_C(0x0f080600f0000103));
   expect("full G2 raw ID", kbase_gpuprops_gpu_id(buffer, n),
          UINT64_C(0x0f080600f0000103));
   for (unsigned size = 0; size < n; ++size)
      expect("truncated G2 stream never aliases Midgard",
             kbase_gpuprops_gpu_id(buffer, size), 0);
   expect("empty stream", kbase_gpuprops_gpu_id(buffer, 0), 0);
   expect("unknown property default", kbase_gpuprop_get(buffer, n, 1000, 99), 99);
}

static void
test_device(const char *path)
{
   struct gpuprops_request {
      uint64_t buffer;
      uint32_t size;
      uint32_t flags;
   } request = {0};
   const unsigned long command = _IOW(0x80, 3, struct gpuprops_request);
   int fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
      failures++;
      return;
   }
   int size = ioctl(fd, command, &request);
   if (size <= 0 || size > 1024 * 1024) {
      fprintf(stderr, "GET_GPUPROPS size failed: result=%d errno=%d\n", size, errno);
      failures++;
      close(fd);
      return;
   }
   uint8_t *buffer = calloc(1, size);
   if (!buffer) {
      failures++;
      close(fd);
      return;
   }
   request.buffer = (uintptr_t)buffer;
   request.size = size;
   int result = ioctl(fd, command, &request);
   close(fd);
   if (result < 0) {
      fprintf(stderr, "GET_GPUPROPS read failed: errno=%d\n", errno);
      failures++;
      free(buffer);
      return;
   }
   uint64_t id = kbase_gpuprops_gpu_id(buffer, size);
   unsigned variant = kbase_gpuprop_get(buffer, size, KBASE_GPUPROP_RAW_CORE_FEATURES, 0) & 0xff;
   const struct pan_model *model = pan_get_model(id, variant, 16, 0);
   printf("LIVE GPU ID: %#018" PRIx64 " architecture=v%u variant=%u revision=r%up%u model=%s\n",
          id, pan_arch(id), variant, PAN_VERSION_MAJOR(id), PAN_VERSION_MINOR(id),
          model ? model->name : "unsupported (no false T600 match)");
   expect("live G2 GPU ID", id, UINT64_C(0x0f080600f0000103));
   expect("live G2 architecture", pan_arch(id), 15);
   expect("live G2 product", pan_prod_id(id), 0x0f0800);
   expect("live unsupported model", model == NULL, true);
   free(buffer);
}

int
main(int argc, char **argv)
{
   test_models();
   test_property_streams();
   if (argc == 2)
      test_device(argv[1]);
   printf("%u checks, %u failures\n", checks, failures);
   return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
