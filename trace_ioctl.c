#define _GNU_SOURCE
#define BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

typedef int (*ioctl_fn)(int, int, ...);
static ioctl_fn original_ioctl;
static int (*original_munmap)(void *, size_t);
static void *(*original_mmap)(void *, size_t, int, int, int, off_t);
static void *(*original_mmap64)(void *, size_t, int, int, int, off64_t);
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t map_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic unsigned event_counter;
static _Atomic unsigned kick_counter;
static _Thread_local int inside;
static char directory[2048];

static void initialize(void)
{
    original_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    original_munmap = dlsym(RTLD_NEXT, "munmap");
    original_mmap = dlsym(RTLD_NEXT, "mmap");
    original_mmap64 = dlsym(RTLD_NEXT, "mmap64");
}

__attribute__((constructor)) static void initialize_directory(void)
{
    pthread_once(&once, initialize);
    const char *value = getenv("MALI_G2_TRACE_DIR");
    if (value)
        snprintf(directory, sizeof(directory), "%s", value);
}

static ssize_t read_self(void *destination, const void *source, size_t size)
{
    struct iovec local = {.iov_base = destination, .iov_len = size};
    struct iovec remote = {.iov_base = (void *)source, .iov_len = size};
    ssize_t result = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    if (result < 0) {
        int memory = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
        if (memory >= 0) {
            result = pread(memory, destination, size, (off_t)(uintptr_t)source);
            int error = errno;
            close(memory);
            errno = error;
        }
    }
    return result;
}

static void snapshot(unsigned kick)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return;
    char path[2300];
    snprintf(path, sizeof(path), "%s/kick-%u-maps.jsonl", directory, kick);
    FILE *metadata = fopen(path, "w");
    if (!metadata) {
        fclose(maps);
        return;
    }
    unsigned map = 0;
    size_t remaining = 16 * 1024 * 1024;
    snprintf(path, sizeof(path), "%s/kick-%u-process-maps.txt", directory, kick);
    FILE *process_maps = fopen(path, "w");
    char line[4096];
    pthread_mutex_lock(&map_mutex);
    while (remaining && fgets(line, sizeof(line), maps)) {
        if (process_maps)
            fputs(line, process_maps);
        if (!strstr(line, "/dev/mali"))
            continue;
        unsigned long start, end, offset;
        char permissions[5] = {0};
        if (sscanf(line, "%lx-%lx %4s %lx", &start, &end, permissions, &offset) != 4 ||
            end <= start || permissions[0] != 'r')
            continue;
        size_t size = end - start;
        if (size > 1024 * 1024)
            size = 1024 * 1024;
        if (size > remaining)
            size = remaining;
        void *copy = malloc(size);
        if (!copy)
            break;
        errno = 0;
        ssize_t copied = read_self(copy, (void *)start, size);
        if (copied < 0) {
            memcpy(copy, (const void *)start, size);
            copied = size;
            errno = 0;
        }
        int error = errno;
        fprintf(metadata,
                "{\"index\":%u,\"start\":\"0x%lx\",\"end\":\"0x%lx\","
                "\"offset\":\"0x%lx\",\"permissions\":\"%s\","
                "\"requested\":%zu,\"copied\":%zd,\"errno\":%d}\n",
                map, start, end, offset, permissions, size, copied, error);
        if (copied > 0) {
            snprintf(path, sizeof(path), "%s/kick-%u-map-%u.bin", directory, kick, map);
            FILE *output = fopen(path, "wb");
            if (output) {
                fwrite(copy, 1, copied, output);
                fclose(output);
            }
            remaining -= copied;
        }
        free(copy);
        map++;
    }
    pthread_mutex_unlock(&map_mutex);
    if (process_maps)
        fclose(process_maps);
    fclose(metadata);
    fclose(maps);
}

int munmap(void *address, size_t size)
{
    if (inside)
        return original_munmap ? original_munmap(address, size) :
               (int)syscall(SYS_munmap, address, size);
    inside = 1;
    int entry_errno = errno;
    pthread_once(&once, initialize);
    pthread_mutex_lock(&map_mutex);
    errno = entry_errno;
    int result = original_munmap ? original_munmap(address, size) :
                 (int)syscall(SYS_munmap, address, size);
    int error = errno;
    pthread_mutex_unlock(&map_mutex);
    inside = 0;
    errno = error;
    return result;
}

void *mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    if (inside)
        return original_mmap ? original_mmap(address, size, protection, flags, fd, offset) :
               (void *)syscall(SYS_mmap, address, size, protection, flags, fd, offset);
    inside = 1;
    int entry_errno = errno;
    pthread_once(&once, initialize);
    if (flags & MAP_FIXED)
        pthread_mutex_lock(&map_mutex);
    errno = entry_errno;
    void *result = original_mmap ? original_mmap(address, size, protection, flags, fd, offset) :
                   (void *)syscall(SYS_mmap, address, size, protection, flags, fd, offset);
    int error = errno;
    if (flags & MAP_FIXED)
        pthread_mutex_unlock(&map_mutex);
    inside = 0;
    errno = error;
    return result;
}

void *mmap64(void *address, size_t size, int protection, int flags, int fd, off64_t offset)
{
    if (inside)
        return original_mmap64 ? original_mmap64(address, size, protection, flags, fd, offset) :
               (void *)syscall(SYS_mmap, address, size, protection, flags, fd, offset);
    inside = 1;
    int entry_errno = errno;
    pthread_once(&once, initialize);
    if (flags & MAP_FIXED)
        pthread_mutex_lock(&map_mutex);
    errno = entry_errno;
    void *result = original_mmap64 ? original_mmap64(address, size, protection, flags, fd, offset) :
                   (void *)syscall(SYS_mmap, address, size, protection, flags, fd, offset);
    int error = errno;
    if (flags & MAP_FIXED)
        pthread_mutex_unlock(&map_mutex);
    inside = 0;
    errno = error;
    return result;
}

static void hex(FILE *file, const unsigned char *bytes, ssize_t count)
{
    for (ssize_t i = 0; i < count; ++i)
        fprintf(file, "%02x", bytes[i]);
}

int ioctl(int fd, int operation, ...)
{
    va_list arguments;
    va_start(arguments, operation);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    if (inside)
        return original_ioctl ? original_ioctl(fd, operation, argument) :
               (int)syscall(SYS_ioctl, fd, operation, argument);
    inside = 1;
    int entry_errno = errno;
    pthread_once(&once, initialize);
    unsigned request = (unsigned)operation;
    int trace = directory[0] && _IOC_TYPE(request) == 0x80;
    char target[128], descriptor[64];
    if (trace) {
        snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", fd);
        ssize_t count = readlink(descriptor, target, sizeof(target) - 1);
        if (count <= 0) {
            trace = 0;
        } else {
            target[count] = 0;
            trace = strncmp(target, "/dev/mali", 9) == 0;
        }
    }
    unsigned size = _IOC_SIZE(request);
    if (size > 4096)
        size = 4096;
    unsigned char before[4096], after[4096];
    ssize_t before_count = 0, after_count = 0;
    unsigned event = 0;
    if (trace) {
        event = atomic_fetch_add(&event_counter, 1);
        if (argument && size)
            before_count = read_self(before, argument, size);
        if (_IOC_NR(request) == 37) {
            unsigned kick = atomic_fetch_add(&kick_counter, 1);
            if (kick < 4)
                snapshot(kick);
        }
    }
    errno = entry_errno;
    int result = original_ioctl ? original_ioctl(fd, operation, argument) :
                 (int)syscall(SYS_ioctl, fd, operation, argument);
    int result_errno = errno;
    if (trace) {
        if (argument && size)
            after_count = read_self(after, argument, size);
        pthread_mutex_lock(&log_mutex);
        char path[2300];
        snprintf(path, sizeof(path), "%s/ioctls.jsonl", directory);
        FILE *file = fopen(path, "a");
        if (file) {
            fprintf(file,
                    "{\"event\":%u,\"tid\":%ld,\"fd\":%d,\"request\":\"0x%08x\","
                    "\"nr\":%u,\"size\":%u,\"argument\":\"%p\","
                    "\"result\":%d,\"errno\":%d,\"before_copied\":%zd,\"before\":\"",
                    event, syscall(SYS_gettid), fd, request, _IOC_NR(request), size,
                    argument, result, result_errno, before_count);
            hex(file, before, before_count);
            fprintf(file, "\",\"after_copied\":%zd,\"after\":\"", after_count);
            hex(file, after, after_count);
            fprintf(file, "\"}\n");
            fclose(file);
        }
        pthread_mutex_unlock(&log_mutex);
    }
    errno = result_errno;
    inside = 0;
    return result;
}
