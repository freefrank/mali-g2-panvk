#include "panvk_g2_trace.h"

#include "util/u_debug.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t trace_once = PTHREAD_ONCE_INIT;
static int trace_fd = -1;

static void
init_trace(void)
{
   if (debug_get_bool_option("PANVK_G2_TRACE", true))
      trace_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
}

void
panvk_g2_trace(const char *format, ...)
{
   int saved_errno = errno;
   pthread_once(&trace_once, init_trace);
   if (trace_fd < 0)
      goto out;

   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   char message[1024];
   int prefix = snprintf(message, sizeof(message),
                         "PANVK_G2_V1 pid=%ld t=%lld.%06ld ", (long)getpid(),
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
   sendto(trace_fd, message, strlen(message), MSG_DONTWAIT,
          (const struct sockaddr *)&destination, sizeof(destination));
out:
   errno = saved_errno;
}

__attribute__((destructor)) static void
finish_trace(void)
{
   if (trace_fd >= 0)
      close(trace_fd);
}
