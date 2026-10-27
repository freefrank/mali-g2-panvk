#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_serialize.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/model/pan_model.h"
#include "util/u_dynarray.h"

static void crash(int signal, siginfo_t *info, void *context)
{
    (void)context;
    dprintf(STDERR_FILENO, "REPLAY_CRASH signal=%d address=%p\n", signal, info->si_addr);
#if defined(__aarch64__)
    ucontext_t *state = context;
    dprintf(STDERR_FILENO, "pc=%#lx lr=%#lx\n", (unsigned long)state->uc_mcontext.pc,
            (unsigned long)state->uc_mcontext.regs[30]);
#endif
    void *frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    _exit(128 + signal);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        return 2;
    struct sigaction action = {.sa_sigaction = crash, .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGILL, &action, NULL);
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END))
        return 1;
    long size = ftell(file);
    if (size < 28 || size > 2097152 || fseek(file, 0, SEEK_SET))
        return 1;
    void *data = malloc(size);
    if (!data || fread(data, 1, size, file) != (size_t)size)
        return 1;
    fclose(file);
    struct blob_reader reader;
    blob_reader_init(&reader, data, size);
    const void *magic = blob_read_bytes(&reader, 8);
    if (!magic || memcmp(magic, "G2NIR001", 8))
        return 1;
    uint32_t stage = blob_read_uint32(&reader);
    uint32_t point = blob_read_uint32(&reader);
    uint32_t inputs_size = blob_read_uint32(&reader);
    uint32_t info_size = blob_read_uint32(&reader);
    uint32_t layout_size = blob_read_uint32(&reader);
    struct pan_compile_inputs inputs;
    struct pan_shader_info info;
    struct pan_varying_layout layout;
    if (inputs_size != sizeof(inputs) || info_size != sizeof(info) ||
        (layout_size && layout_size != sizeof(layout)) || stage >= MESA_SHADER_STAGES)
        return 1;
    blob_copy_bytes(&reader, &inputs, sizeof(inputs));
    blob_copy_bytes(&reader, &info, sizeof(info));
    if (layout_size)
        blob_copy_bytes(&reader, &layout, sizeof(layout));
    if (reader.overrun || inputs.arch != 15 || inputs.model || inputs.varying_layout)
        return 1;
    inputs.model = pan_get_model(UINT64_C(0x0f080600f0000103), 0, 16, 0);
    inputs.varying_layout = layout_size ? &layout : NULL;
    glsl_type_singleton_init_or_ref();
    const nir_shader_compiler_options *options =
        pan_get_nir_shader_compiler_options(inputs.arch, stage, inputs.is_blend);
    nir_shader *nir = nir_deserialize(NULL, options, &reader);
    if (!nir || reader.overrun || reader.current != reader.end)
        return 1;
    if (getenv("MALI_G2_REPLAY_PRINT_NIR"))
        nir_print_shader(nir, stderr);
    if (!point) {
        printf("PIPELINE_NIR_READ_PASS stage=%u\n", stage);
        ralloc_free(nir);
        glsl_type_singleton_decref();
        free(data);
        return 0;
    }
    fprintf(stderr, "REPLAY_BEGIN stage=%u fau_reserved=%u\n", stage, inputs.fau.reserved);
    struct util_dynarray binary = UTIL_DYNARRAY_INIT;
    pan_shader_compile(nir, &inputs, &binary, &info);
    printf("REPLAY_PASS stage=%u regs=%u tls=%u fau=%u bytes=%u\n", stage,
           info.work_reg_count, info.tls_size, info.fau.count, binary.size);
    if (argc == 3) {
        FILE *output = fopen(argv[2], "wb");
        if (!output || fwrite(binary.data, 1, binary.size, output) != binary.size ||
            fclose(output))
            return 1;
    }
    util_dynarray_fini(&binary);
    ralloc_free(nir);
    glsl_type_singleton_decref();
    free(data);
    return 0;
}
