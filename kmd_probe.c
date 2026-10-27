#define _GNU_SOURCE
#define BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct allocation {
    void *cpu;
    size_t size;
    uint64_t gpu;
    uint64_t flags;
};

static int request(int fd, unsigned long code, void *argument, const char *name)
{
    errno = 0;
    int result = ioctl(fd, (int)code, argument);
    int error = errno;
    printf("%s request=%#lx result=%d errno=%d\n", name, code, result, error);
    errno = error;
    return result;
}

static struct allocation allocate(int fd, size_t size, uint64_t flags, int extended)
{
    uint64_t args[8] = {size / 4096, size / 4096, 0, flags};
    unsigned long code = _IOC(_IOC_READ | _IOC_WRITE, 0x80, extended ? 59 : 5,
                              extended ? 64 : 32);
    struct allocation a = {.size = size};
    if (request(fd, code, args, extended ? "MEM_ALLOC_EX" : "MEM_ALLOC") < 0)
        return a;
    a.flags = args[0];
    void *cpu = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, args[1]);
    printf("mmap cookie=%#" PRIx64 " flags=%#" PRIx64 " result=%p errno=%d\n",
           args[1], a.flags, cpu, errno);
    if (cpu == MAP_FAILED)
        return a;
    a.cpu = cpu;
    a.gpu = a.flags & (1ull << 13) ? (uint64_t)(uintptr_t)cpu : args[1];
    memset(cpu, 0, size);
    return a;
}

static int64_t monotonic_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    unsigned minor = argc > 1 ? strtoul(argv[1], NULL, 0) : 39;
    int extended = argc > 2 ? atoi(argv[2]) : 1;
    int modern_group = argc > 3 ? atoi(argv[3]) : 1;
    unsigned register_allocation = argc > 5 ? strtoul(argv[5], NULL, 0) : 1;
    int fd = open("/dev/mali0", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) { perror("/dev/mali0"); return 1; }
    uint16_t version[2] = {1, minor};
    uint32_t flags = 0;
    int result = 1;
    void *tracking = MAP_FAILED, *io = MAP_FAILED;
    struct allocation ring = {0}, event = {0}, descriptors = {0}, shader = {0};
    uint64_t group[16] = {0};
    int group_created = 0, registered = 0;
    if (request(fd, _IOWR(0x80, 52, version), version, "VERSION") < 0 ||
        request(fd, _IOW(0x80, 1, flags), &flags, "SET_FLAGS") < 0)
        goto out;
    printf("version=%u.%u\n", version[0], version[1]);
    tracking = mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, 0x3000);
    if (tracking == MAP_FAILED) { perror("tracking mmap"); goto out; }
    ring = allocate(fd, 9 * 4096, 0x20200f, extended);
    event = allocate(fd, 4096, 0x8200f, extended);
    if (!ring.cpu || !event.cpu)
        goto out;
    if (argc > 4) {
        uint64_t exec_pages = 0x100000;
        if (request(fd, _IOW(0x80, 38, exec_pages), &exec_pages, "MEM_EXEC_INIT") < 0)
            goto out;
        shader = allocate(fd, 4096, 0x17, extended);
        descriptors = allocate(fd, 4096, 0x280f, extended);
        if (!shader.cpu || !descriptors.cpu)
            goto out;
        FILE *file = fopen(argv[4], "rb");
        if (!file) { perror("shader binary"); goto out; }
        size_t code_size = fread(shader.cpu, 1, shader.size, file);
        int invalid = ferror(file) || !feof(file) || !code_size || code_size % 8;
        fclose(file);
        if (invalid) { fprintf(stderr, "invalid shader binary\n"); goto out; }
        printf("shader=%#" PRIx64 " bytes=%zu descriptors=%#" PRIx64 "\n",
               shader.gpu, code_size, descriptors.gpu);
        uint64_t *d = descriptors.cpu;
        d[0x80 / 8] = descriptors.gpu + 0x100;
        d[0x88 / 8] = 32;
        d[0x100 / 8] = UINT64_C(0x0000001000000019);
        d[0x108 / 8] = descriptors.gpu + 0x120;
        d[0x130 / 8] = event.gpu;
        d[0x140 / 8] = UINT64_C(0x0000001f00000000);
        d[0x148 / 8] = UINT64_C(0x1000000000000000);
        d[0x180 / 8] = UINT64_C(0x0000000100100018);
        d[0x188 / 8] = shader.gpu;
        for (unsigned i = 0; i < 32; i++) ((uint32_t *)event.cpu)[i] = 0xdeadbeef;
    }
    group[2] = UINT64_MAX;
    uint8_t *g = (uint8_t *)group;
    g[24] = 1;
    g[25] = 1;
    g[28] = 16;
    unsigned long create = _IOC(_IOC_READ | _IOC_WRITE, 0x80,
                                modern_group ? 63 : 58,
                                modern_group ? 128 : 112);
    if (request(fd, create, group, "GROUP_CREATE") < 0)
        goto out;
    group_created = 1;
    uint8_t handle = g[0];
    printf("group=%u uid=%u\n", handle, ((uint32_t *)group)[1]);
    struct { uint64_t address; uint32_t size; uint8_t priority; uint8_t padding[3]; }
        reg = {ring.gpu, 32768, 8, {0}};
    if (request(fd, _IOW(0x80, 36, reg), &reg, "QUEUE_REGISTER") < 0)
        goto out;
    registered = 1;
    uint64_t bind[2] = {ring.gpu, handle};
    if (request(fd, _IOWR(0x80, 39, bind), bind, "QUEUE_BIND") < 0)
        goto out;
    io = mmap(NULL, 3 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bind[0]);
    if (io == MAP_FAILED) { perror("userio mmap"); goto out; }
    volatile uint64_t *input = (void *)((uint8_t *)io + 4096);
    volatile uint64_t *output = (void *)((uint8_t *)io + 8192);
    uint64_t *cs = ring.cpu;
    cs[0] = UINT64_C(0x2200000000000001);
    unsigned words = 1;
    if (shader.cpu) {
        cs[words++] = UINT64_C(0x1c00000000000000);
        cs[words++] = UINT64_C(0x0110000000000000) |
                      (descriptors.gpu + 0x180) | register_allocation;
        cs[words++] = UINT64_C(0x0118000000000000) | (descriptors.gpu + 0x140);
        cs[words++] = UINT64_C(0x0100000000000000) | (descriptors.gpu + 0x88);
        cs[words++] = UINT64_C(0x0208000000000000) | (uint32_t)(descriptors.gpu + 0x130);
        cs[words++] = UINT64_C(0x0209000000000000) | (1u << 24) |
                      (uint32_t)(descriptors.gpu >> 32);
        cs[words++] = UINT64_C(0x0225000000000001);
        cs[words++] = UINT64_C(0x0226000000000001);
        cs[words++] = UINT64_C(0x0227000000000001);
        cs[words++] = UINT64_C(0x0222000000000000);
        cs[words++] = UINT64_C(0x0223000000000000);
        cs[words++] = UINT64_C(0x0224000000000000);
        cs[words++] = UINT64_C(0x022100000000001f);
        cs[words++] = UINT64_C(0x0400000000018001);
        cs[words++] = UINT64_C(0x0300000000010000);
        cs[words++] = UINT64_C(0x1c00000000000000);
        cs[words++] = UINT64_C(0x017c000000000000);
        cs[words++] = UINT64_C(0x24007c0000000233);
        cs[words++] = UINT64_C(0x0300000000010000);
    }
    cs[words++] = UINT64_C(0x0148000000000000) | (event.gpu + 0x800);
    cs[words++] = UINT64_C(0x024a000013579bdf);
    cs[words++] = UINT64_C(0x2600484a00000001);
    cs[words++] = UINT64_C(0x0300000000010000);
    __sync_synchronize();
    input[0] = (words * 8 + 63) & ~63u;
    __sync_synchronize();
    uint64_t kick = ring.gpu;
    if (request(fd, _IOW(0x80, 37, kick), &kick, "QUEUE_KICK") < 0)
        goto out;
    volatile uint32_t *marker = (void *)((uint8_t *)event.cpu + 0x800);
    int64_t deadline = monotonic_ns() + 2000000000;
    while (*marker != UINT32_C(0x13579bdf) && monotonic_ns() < deadline) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        if (poll(&p, 1, 2) > 0) {
            uint8_t notifications[512];
            ssize_t count = read(fd, notifications, sizeof(notifications));
            if (count > 0) {
                printf("notification bytes=%zd data=", count);
                for (ssize_t i = 0; i < count; i++) printf("%02x", notifications[i]);
                putchar('\n');
            }
        }
    }
    __sync_synchronize();
    result = *marker != UINT32_C(0x13579bdf);
    printf("CSF_MARKER_%s marker=%#x insert=%" PRIu64 " extract=%" PRIu64
           " active=%" PRIu64 "\n", result ? "FAIL" : "PASS", *marker,
           input[0], output[0], output[1]);
    if (!result && shader.cpu) {
        volatile uint32_t *values = event.cpu;
        for (unsigned i = 0; i < 32; i++) {
            if (values[i] != i * i + 7) {
                printf("compute mismatch index=%u actual=%#x expected=%u\n",
                       i, values[i], i * i + 7);
                result = 1;
                break;
            }
        }
        printf("CSF_COMPUTE_%s first=%u last=%u\n", result ? "FAIL" : "PASS",
               values[0], values[31]);
    }
out:
    if (group_created) {
        uint64_t term = ((uint8_t *)group)[0];
        request(fd, _IOW(0x80, 43, term), &term, "GROUP_TERMINATE");
    }
    if (registered) {
        uint64_t term = ring.gpu;
        request(fd, _IOW(0x80, 41, term), &term, "QUEUE_TERMINATE");
    }
    if (io != MAP_FAILED) munmap(io, 3 * 4096);
    if (event.cpu) munmap(event.cpu, event.size);
    if (ring.cpu) munmap(ring.cpu, ring.size);
    if (descriptors.cpu) munmap(descriptors.cpu, descriptors.size);
    if (shader.cpu) {
        munmap(shader.cpu, shader.size);
        request(fd, _IOW(0x80, 7, shader.gpu), &shader.gpu, "MEM_FREE");
    }
    if (tracking != MAP_FAILED) munmap(tracking, 4096);
    close(fd);
    return result;
}
