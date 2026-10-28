#include "panvk_g2_trace.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_serialize.h"
#include "panfrost/compiler/pan_compiler.h"
#include "util/macros.h"
#include "util/u_debug.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <ucontext.h>
#include <unwind.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t trace_once = PTHREAD_ONCE_INIT;
static int trace_fd = -1;
static int trace_file_fd = -1;
static atomic_size_t trace_file_bytes;
static unsigned capture_first, capture_last;
static bool capture_pipeline;

static const int crash_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
static struct sigaction crash_prev[ARRAY_SIZE(crash_signals)];

static void
trace_addr(const char *what, unsigned idx, uintptr_t addr)
{
   Dl_info info;
   if (addr && dladdr((void *)addr, &info) && info.dli_fname) {
      const char *name = strrchr(info.dli_fname, '/');
      panvk_g2_trace("crash %s=%u addr=0x%" PRIxPTR " lib=%s off=0x%" PRIxPTR
                     " sym=%s", what, idx, addr,
                     name ? name + 1 : info.dli_fname,
                     addr - (uintptr_t)info.dli_fbase,
                     info.dli_sname ? info.dli_sname : "?");
   } else {
      panvk_g2_trace("crash %s=%u addr=0x%" PRIxPTR, what, idx, addr);
   }
}

struct crash_unwind {
   unsigned depth;
};

static _Unwind_Reason_Code
crash_unwind_frame(struct _Unwind_Context *ctx, void *data)
{
   struct crash_unwind *state = data;
   uintptr_t pc = _Unwind_GetIP(ctx);
   if (!pc || state->depth >= 48)
      return _URC_END_OF_STACK;
   trace_addr("frame", state->depth++, pc);
   return _URC_NO_REASON;
}

static atomic_uint crash_reports;

static bool
in_library(uintptr_t addr)
{
   Dl_info info;
   return addr && dladdr((void *)addr, &info) && info.dli_fname;
}

static void
crash_handler(int sig, siginfo_t *si, void *uc_ptr)
{
   const ucontext_t *uc = uc_ptr;
   unsigned i = 0;
   while (crash_signals[i] != sig)
      i++;
   const struct sigaction *prev = &crash_prev[i];

   if ((sig == SIGABRT || in_library(uc->uc_mcontext.pc)) &&
       atomic_fetch_add(&crash_reports, 1) < 4) {
      panvk_g2_trace("crash signal=%d code=%d fault=0x%" PRIxPTR, sig,
                     si->si_code, (uintptr_t)si->si_addr);
      trace_addr("pc", 0, uc->uc_mcontext.pc);
      trace_addr("lr", 0, uc->uc_mcontext.regs[30]);
      struct crash_unwind state = {0};
      _Unwind_Backtrace(crash_unwind_frame, &state);
      panvk_g2_trace("crash end frames=%u", state.depth);
   }

   if (prev->sa_flags & SA_SIGINFO) {
      prev->sa_sigaction(sig, si, uc_ptr);
   } else if (prev->sa_handler == SIG_DFL) {
      sigaction(sig, prev, NULL);
      if (sig == SIGABRT)
         raise(sig);
   } else if (prev->sa_handler != SIG_IGN) {
      prev->sa_handler(sig);
   }
}

static void
install_crash_handler(void)
{
   struct sigaction action = {
      .sa_sigaction = crash_handler,
      .sa_flags = SA_SIGINFO | SA_ONSTACK,
   };
   sigemptyset(&action.sa_mask);
   for (unsigned i = 0; i < ARRAY_SIZE(crash_signals); i++)
      sigaction(crash_signals[i], &action, &crash_prev[i]);
}

static void
init_trace(void)
{
   if (!debug_get_bool_option("PANVK_G2_TRACE", true))
      return;

   if (debug_get_bool_option("PANVK_G2_CRASH_HANDLER", true))
      install_crash_handler();

   trace_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
   capture_first = debug_get_num_option("PANVK_G2_CAPTURE_FIRST", 1);
   capture_last = debug_get_num_option("PANVK_G2_CAPTURE_LAST", UINT32_MAX);
   capture_pipeline = debug_get_bool_option("PANVK_G2_CAPTURE_PIPELINE", false);
   const char *path = debug_get_option(
      "PANVK_G2_TRACE_FILE",
      "/storage/emulated/0/Download/panvk-g2-driver-events.log");
   if (path[0])
      trace_file_fd = open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
   if (trace_file_fd >= 0) {
      struct stat statbuf;
      if (fstat(trace_file_fd, &statbuf) || !S_ISREG(statbuf.st_mode) ||
          statbuf.st_size >= 8388608) {
         close(trace_file_fd);
         trace_file_fd = -1;
      } else {
         atomic_store_explicit(&trace_file_bytes, statbuf.st_size,
                               memory_order_relaxed);
      }
   }
}

void
panvk_g2_trace(const char *format, ...)
{
   int saved_errno = errno;
   pthread_once(&trace_once, init_trace);
   if (trace_fd < 0 && trace_file_fd < 0)
      goto out;

   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   char message[1024];
   int prefix = snprintf(message, sizeof(message),
                         "PANVK_G2_V1 pid=%ld tid=%ld t=%lld.%06ld ",
                         (long)getpid(), syscall(SYS_gettid),
                         (long long)now.tv_sec, now.tv_nsec / 1000);
   if (prefix < 0 || (size_t)prefix >= sizeof(message))
      goto out;
   va_list args;
   va_start(args, format);
   vsnprintf(message + prefix, sizeof(message) - prefix, format, args);
   va_end(args);

   const struct sockaddr_in destination = {
      .sin_family = AF_INET,
      .sin_port = htons(28643),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
   };
   size_t length = strlen(message);
   if (trace_fd >= 0)
      sendto(trace_fd, message, length, MSG_DONTWAIT,
             (const struct sockaddr *)&destination, sizeof(destination));
   if (trace_file_fd >= 0 &&
       atomic_fetch_add_explicit(&trace_file_bytes, length + 1,
                                 memory_order_relaxed) < 8388608) {
      const struct iovec lines[] = {
         {.iov_base = message, .iov_len = length},
         {.iov_base = "\n", .iov_len = 1},
      };
      writev(trace_file_fd, lines, 2);
   }
out:
   errno = saved_errno;
}

void
panfrost_valhall_compile_error(const char *reason, const char *instruction)
{
   panvk_g2_trace("compiler error reason=%s instruction=%s", reason, instruction);
}

void
panvk_g2_trace_shader(uint32_t id, uint32_t point, const nir_shader *nir,
                      const struct pan_compile_inputs *inputs,
                      const struct pan_shader_info *info)
{
   int saved_errno = errno;
   pthread_once(&trace_once, init_trace);
   if (trace_fd < 0 || id < capture_first || id > capture_last ||
       (!point && !capture_pipeline))
      goto out;

   struct pan_compile_inputs saved_inputs = {.arch = 15};
   if (inputs)
      saved_inputs = *inputs;
   saved_inputs.model = NULL;
   saved_inputs.varying_layout = NULL;
   struct pan_shader_info saved_info = {0};
   if (info)
      saved_info = *info;

   struct blob blob;
   blob_init(&blob);
   blob_write_bytes(&blob, "G2NIR001", 8);
   blob_write_uint32(&blob, nir->info.stage);
   blob_write_uint32(&blob, point);
   blob_write_uint32(&blob, sizeof(saved_inputs));
   blob_write_uint32(&blob, sizeof(saved_info));
   bool has_layout = inputs && inputs->varying_layout;
   blob_write_uint32(&blob, has_layout ? sizeof(*inputs->varying_layout) : 0);
   blob_write_bytes(&blob, &saved_inputs, sizeof(saved_inputs));
   blob_write_bytes(&blob, &saved_info, sizeof(saved_info));
   if (has_layout)
      blob_write_bytes(&blob, inputs->varying_layout,
                       sizeof(*inputs->varying_layout));
   nir_serialize(&blob, nir, true);

   if (blob.out_of_memory || blob.size > 2097152) {
      panvk_g2_trace("shader dump skipped id=%u point=%u bytes=%zu", id,
                     point, blob.size);
      blob_finish(&blob);
      goto out;
   }

   panvk_g2_trace("shader dump begin id=%u stage=%u point=%u bytes=%zu", id,
                  nir->info.stage, point, blob.size);
   const struct sockaddr_in destination = {
      .sin_family = AF_INET,
      .sin_port = htons(28643),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
   };
   for (size_t offset = 0; offset < blob.size; offset += 4096) {
      char packet[4224];
      int header = snprintf(packet, 128,
                            "PANVK_G2_BLOB_V1 %ld %u %u %u %zu %zu\n",
                            (long)getpid(), id, nir->info.stage, point,
                            blob.size, offset);
      if (header < 0 || header >= 128)
         break;
      size_t length = MIN2(blob.size - offset, 4096);
      memcpy(packet + header, blob.data + offset, length);
      ssize_t sent = sendto(trace_fd, packet, header + length, MSG_DONTWAIT,
                            (const struct sockaddr *)&destination,
                            sizeof(destination));
      if (sent < 0 || (size_t)sent != header + length) {
         panvk_g2_trace("shader dump send failed id=%u stage=%u point=%u errno=%d",
                        id, nir->info.stage, point, errno);
         break;
      }
   }
   blob_finish(&blob);
out:
   errno = saved_errno;
}

__attribute__((destructor)) static void
finish_trace(void)
{
   if (trace_fd >= 0)
      close(trace_fd);
   if (trace_file_fd >= 0)
      close(trace_file_fd);
}
