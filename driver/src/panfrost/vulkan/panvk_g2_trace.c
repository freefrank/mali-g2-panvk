#include "panvk_g2_trace.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_serialize.h"
#include "panfrost/compiler/pan_compiler.h"
#include "util/macros.h"
#include "util/u_debug.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
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
bool panvk_g2_diagnostics;

void
panvk_g2_trace_eden_log(void)
{
   if (!panvk_g2_diagnostics)
      return;
   static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
   static time_t last_check;
   static off_t offsets[2];
   static ino_t inodes[2];
   static int errors[2];
   static char source[2][PATH_MAX];
   char paths[2][PATH_MAX] = {
      "/storage/emulated/0/Android/data/com.miHoYo.Yuanshen.nightly/files/log/eden_log.txt",
      "/storage/emulated/0/Android/data/com.miHoYo.Yuanshen.nightly/files/log/eden_log.txt.old.txt",
   };

   if (pthread_mutex_trylock(&lock))
      return;

   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   if (now.tv_sec - last_check < 5) {
      pthread_mutex_unlock(&lock);
      return;
   }
   last_check = now.tv_sec;

   DIR *dir = opendir("/proc/self/fd");
   if (dir) {
      struct dirent *entry;
      while ((entry = readdir(dir))) {
         char fdpath[64], target[PATH_MAX];
         int n = snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%s", entry->d_name);
         if (n < 0 || n >= sizeof(fdpath))
            continue;
         ssize_t length = readlink(fdpath, target, sizeof(target) - 1);
         if (length < 0)
            continue;
         target[length] = 0;
         const char *name = strrchr(target, '/');
         if (!name)
            continue;
         name++;
         unsigned i;
         if (!strcmp(name, "eden_log.txt") || !strcmp(name, "yuzu_log.txt"))
            i = 0;
         else if (!strcmp(name, "eden_log.txt.old.txt") ||
                  !strcmp(name, "yuzu_log.txt.old.txt"))
            i = 1;
         else
            continue;
         snprintf(paths[i], sizeof(paths[i]), "%s", target);
         if (strcmp(source[i], target)) {
            snprintf(source[i], sizeof(source[i]), "%s", target);
            panvk_g2_trace("eden log=%u source=%s", i, target);
         }
         if (i == 0 && strlen(target) + sizeof(".old.txt") <= sizeof(paths[1]))
            snprintf(paths[1], sizeof(paths[1]), "%s.old.txt", target);
      }
      closedir(dir);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(paths); i++) {
      int fd = syscall(SYS_openat, AT_FDCWD, paths[i], O_RDONLY | O_CLOEXEC, 0);
      FILE *f = fd >= 0 ? fdopen(fd, "r") : NULL;
      if (!f) {
         int error = errno;
         if (fd >= 0)
            close(fd);
         if (errors[i] != error) {
            errors[i] = error;
            panvk_g2_trace("eden log=%u unavailable errno=%d", i, errors[i]);
         }
         continue;
      }

      struct stat st;
      if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode)) {
         fclose(f);
         continue;
      }
      errors[i] = 0;
      if (inodes[i] != st.st_ino || offsets[i] > st.st_size) {
         inodes[i] = st.st_ino;
         offsets[i] = i && st.st_size > 262144 ? st.st_size - 262144 : 0;
      }
      if (fseeko(f, offsets[i], SEEK_SET)) {
         fclose(f);
         continue;
      }

      off_t end = offsets[i] + 262144;
      char line[2048];
      while (ftello(f) < end && fgets(line, sizeof(line), f)) {
         line[strcspn(line, "\r\n")] = 0;
         if (strstr(line, "Render.") || strstr(line, "Shader") ||
             strstr(line, "NVDEC") || strstr(line, "Nvdec") ||
             strstr(line, "nvdec") || strstr(line, "Renderer.") ||
             strstr(line, "Codec") || strstr(line, "Video") ||
             strstr(line, "<Error>") || strstr(line, "<Warning>") ||
             strstr(line, "<Critical>"))
            panvk_g2_trace("eden log=%u %s", i, line);
      }
      off_t pos = ftello(f);
      if (pos >= 0)
         offsets[i] = pos;
      fclose(f);
   }

   pthread_mutex_unlock(&lock);
}

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
   if (!panvk_g2_diagnostics || !debug_get_bool_option("PANVK_G2_TRACE", true))
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

void
panvk_g2_trace_shader(uint32_t id, uint32_t point, const nir_shader *nir,
                      const struct pan_compile_inputs *inputs,
                      const struct pan_shader_info *info)
{
   if (!panvk_g2_diagnostics)
      return;
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

#include "panfrost/lib/kmod/kbase_kmod.h"

static atomic_uint check_reports;

struct check_bt {
   unsigned depth;
   size_t len;
   char text[512];
};

static _Unwind_Reason_Code
check_bt_frame(struct _Unwind_Context *ctx, void *data)
{
   struct check_bt *bt = data;
   uintptr_t pc = _Unwind_GetIP(ctx);
   if (!pc || bt->depth++ >= 14 || bt->len + 48 >= sizeof(bt->text))
      return _URC_END_OF_STACK;
   Dl_info info;
   if (dladdr((void *)pc, &info) && info.dli_fname) {
      const char *name = strrchr(info.dli_fname, '/');
      bt->len += snprintf(bt->text + bt->len, sizeof(bt->text) - bt->len, " %s+0x%" PRIxPTR,
                          name ? name + 1 : info.dli_fname, pc - (uintptr_t)info.dli_fbase);
   }
   return _URC_NO_REASON;
}

void
panvk_g2_check_range(uint64_t addr, uint64_t size, const char *kind, uint32_t binding,
                     uint32_t elem)
{
   if (!panvk_g2_diagnostics)
      return;
   uint64_t bo_va, bo_size;
   if (!addr || pan_kmod_kbase_va_live(addr, size ? size : 1, &bo_va, &bo_size))
      return;
   if (atomic_fetch_add(&check_reports, 1) >= 256)
      return;
   struct check_bt bt = {0};
   _Unwind_Backtrace(check_bt_frame, &bt);
   panvk_g2_trace("badrange kind=%s addr=0x%" PRIx64 " size=0x%" PRIx64 " binding=%u elem=%u "
                  "bo=0x%" PRIx64 "+0x%" PRIx64 " bt=%s",
                  kind, addr, size, binding, elem, bo_va, bo_size, bt.text);
}

static void
check_texture_plane(uint64_t addr, uint64_t size, const char *kind)
{
   panvk_g2_check_range(addr, size, kind, ~0u, ~0u);
}

extern void (*pan_g2_check_texture_plane)(uint64_t addr, uint64_t size, const char *kind);

void
panvk_g2_check_install(void)
{
   if (panvk_g2_diagnostics)
      pan_g2_check_texture_plane = check_texture_plane;
}

#include <sys/uio.h>

/* Kbase SAME_VA BOs live at the same CPU and GPU address, so GPU memory can be read back after
 * a fault. */
static bool
fault_read(uint64_t addr, void *dst, size_t size)
{
   uint64_t bo_va, bo_size;
   if (!addr || !pan_kmod_kbase_va_live(addr, size, &bo_va, &bo_size))
      return false;
   struct iovec local = {.iov_base = dst, .iov_len = size};
   struct iovec remote = {.iov_base = (void *)(uintptr_t)addr, .iov_len = size};
   if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)size)
      return true;
   /* The app sandbox may refuse process_vm_readv; every live BO keeps its RW anchor. */
   memcpy(dst, (const void *)(uintptr_t)addr, size);
   return true;
}

void
panvk_g2_fault_words(uint32_t work, const char *tag, uint64_t addr, uint32_t words)
{
   if (!panvk_g2_diagnostics)
      return;
   uint32_t w[32];
   words = MIN2(words, ARRAY_SIZE(w));
   if (!fault_read(addr, w, words * 4)) {
      panvk_g2_trace("fault words work=%u %s addr=0x%" PRIx64 " unreadable", work, tag, addr);
      return;
   }
   char text[400];
   size_t len = 0;
   for (uint32_t i = 0; i < words; i++)
      len += snprintf(text + len, sizeof(text) - len, " %08x", w[i]);
   panvk_g2_trace("fault words work=%u %s addr=0x%" PRIx64 "%s", work, tag, addr, text);
}

static void
fault_check(uint32_t work, const char *tag, uint32_t r, uint32_t d, const char *what,
            uint64_t addr, uint64_t size, const uint32_t *w)
{
   uint64_t bo_va, bo_size;
   bool live = pan_kmod_kbase_va_live(addr, size ? size : 1, &bo_va, &bo_size);
   if (live && addr)
      return;
   panvk_g2_trace("fault baddesc work=%u %s r=%u d=%u %s addr=0x%" PRIx64 " size=0x%" PRIx64
                  " bo=0x%" PRIx64 "+0x%" PRIx64 " w=%08x %08x %08x %08x %08x %08x %08x %08x",
                  work, tag, r, d, what, addr, size, bo_va, bo_size, w[0], w[1], w[2], w[3],
                  w[4], w[5], w[6], w[7]);
}

void
panvk_g2_fault_res_table(uint32_t work, const char *tag, uint64_t res_table)
{
   if (!panvk_g2_diagnostics)
      return;
   uint64_t table = res_table & ~63ull;
   uint32_t count = res_table & 63;
   if (!table)
      return;
   for (uint32_t r = 0; r < count; r++) {
      uint32_t res[4];
      if (!fault_read(table + r * 16, res, sizeof(res))) {
         panvk_g2_trace("fault res work=%u %s r=%u table=0x%" PRIx64 " unreadable", work, tag, r,
                        table);
         return;
      }
      uint64_t addr = res[0] | (uint64_t)(res[1] & 0xffffff) << 32;
      uint64_t size = res[2] | (uint64_t)res[3] << 32;
      uint32_t ndesc = size / 32, buffers = 0, textures = 0, other = 0;
      if (!addr)
         continue;
      for (uint32_t d = 0; d < MIN2(ndesc, 256); d++) {
         uint32_t w[8];
         if (!fault_read(addr + d * 32, w, sizeof(w))) {
            panvk_g2_trace("fault res work=%u %s r=%u d=%u addr=0x%" PRIx64 " unreadable", work,
                           tag, r, d, addr);
            break;
         }
         switch (w[0] & 0xf) {
         case 9:
            buffers++;
            if (w[1])
               fault_check(work, tag, r, d, "buffer", w[2] | (uint64_t)w[3] << 32, w[1], w);
            break;
         case 2: {
            textures++;
            uint64_t surf = w[4] | (uint64_t)w[5] << 32;
            uint32_t p[8];
            if (!fault_read(surf, p, sizeof(p))) {
               fault_check(work, tag, r, d, "surfaces", surf, 32, w);
               break;
            }
            if ((p[0] & 0xf) == 10 && ((p[0] >> 4) & 0xf))
               fault_check(work, tag, r, d, "plane",
                           p[2] | (uint64_t)(p[3] & 0xffff) << 32,
                           p[1] | (((p[0] >> 4) & 0xf) == 1 ? (uint64_t)(p[3] >> 16) << 32 : 0),
                           p);
            break;
         }
         default:
            other++;
            break;
         }
      }
      panvk_g2_trace("fault res work=%u %s r=%u addr=0x%" PRIx64 " descs=%u buffers=%u "
                     "textures=%u other=%u", work, tag, r, addr, ndesc, buffers, textures, other);
   }
}
