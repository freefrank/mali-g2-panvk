#include "panvk_g2_trace.h"

#include "util/u_debug.h"

#include <arpa/inet.h>
#include <errno.h>
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
bool panvk_g2_diagnostics;

static void
init_trace(void)
{
   if (!panvk_g2_diagnostics || !debug_get_bool_option("PANVK_G2_TRACE", true))
      return;

   trace_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
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
   if (!panvk_g2_diagnostics)
      return;
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

__attribute__((destructor)) static void
finish_trace(void)
{
   if (trace_fd >= 0)
      close(trace_fd);
   if (trace_file_fd >= 0)
      close(trace_file_fd);
}
