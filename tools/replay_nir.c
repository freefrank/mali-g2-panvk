#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef __ANDROID__
#include <dlfcn.h>
#include <unwind.h>
#else
#include <execinfo.h>
#endif
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
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

#ifdef __ANDROID__
static _Unwind_Reason_Code print_frame(struct _Unwind_Context *frame, void *data)
{
    unsigned *count = data;
    uintptr_t pc = _Unwind_GetIP(frame);
    Dl_info info;
    if (dladdr((void *)pc, &info))
        dprintf(STDERR_FILENO, "frame=%u pc=%#lx object=%s offset=%#lx\n", *count,
                (unsigned long)pc, info.dli_fname,
                (unsigned long)(pc - (uintptr_t)info.dli_fbase));
    else
        dprintf(STDERR_FILENO, "frame=%u pc=%#lx\n", *count, (unsigned long)pc);
    return ++*count >= 64 ? _URC_END_OF_STACK : _URC_NO_REASON;
}
#endif

static void crash(int signal, siginfo_t *info, void *context)
{
    (void)context;
    dprintf(STDERR_FILENO, "REPLAY_CRASH signal=%d address=%p\n", signal, info->si_addr);
#if defined(__aarch64__)
    ucontext_t *state = context;
    dprintf(STDERR_FILENO, "pc=%#lx lr=%#lx\n", (unsigned long)state->uc_mcontext.pc,
            (unsigned long)state->uc_mcontext.regs[30]);
#endif
#ifdef __ANDROID__
    unsigned count = 0;
    _Unwind_Backtrace(print_frame, &count);
#else
    void *frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
    _exit(128 + signal);
}

static int replay_one(const char *path, const char *output_path)
{
    FILE *file = fopen(path, "rb");
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
    fprintf(stderr, "REPLAY_BEGIN file=%s stage=%u fau_reserved=%u\n", path, stage,
            inputs.fau.reserved);
    struct util_dynarray binary = UTIL_DYNARRAY_INIT;
    pan_shader_compile(nir, &inputs, &binary, &info);
    printf("REPLAY_PASS stage=%u regs=%u tls=%u fau=%u bytes=%u\n", stage,
           info.work_reg_count, info.tls_size, info.fau.count, binary.size);
    if (output_path) {
        FILE *output = fopen(output_path, "wb");
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

struct batch {
    char **paths;
    size_t count, next, passed;
    int failure;
    pthread_mutex_t mutex;
};

static void *run_batch(void *data)
{
    struct batch *batch = data;
    void *alternate = malloc(65536);
    if (alternate) {
        stack_t stack = {.ss_sp = alternate, .ss_size = 65536};
        sigaltstack(&stack, NULL);
    }
    for (;;) {
        pthread_mutex_lock(&batch->mutex);
        size_t index = batch->next++;
        int failed = batch->failure;
        pthread_mutex_unlock(&batch->mutex);
        if (failed || index >= batch->count)
            break;
        int result = replay_one(batch->paths[index], NULL);
        pthread_mutex_lock(&batch->mutex);
        if (result)
            batch->failure = result;
        else
            batch->passed++;
        pthread_mutex_unlock(&batch->mutex);
    }
    if (alternate) {
        stack_t stack = {.ss_flags = SS_DISABLE};
        sigaltstack(&stack, NULL);
        free(alternate);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        return 2;
    struct sigaction action = {.sa_sigaction = crash, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGILL, &action, NULL);
    if (strcmp(argv[1], "--batch"))
        return replay_one(argv[1], argc == 3 ? argv[2] : NULL);
    if (argc != 3)
        return 2;
    FILE *list = fopen(argv[2], "r");
    if (!list)
        return 1;
    struct batch batch = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, list) >= 0) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0])
            continue;
        char **paths = realloc(batch.paths, (batch.count + 1) * sizeof(*paths));
        if (!paths)
            return 1;
        batch.paths = paths;
        batch.paths[batch.count++] = strdup(line);
        if (!batch.paths[batch.count - 1])
            return 1;
    }
    free(line);
    fclose(list);
    unsigned threads = getenv("MALI_G2_REPLAY_THREADS") ?
        strtoul(getenv("MALI_G2_REPLAY_THREADS"), NULL, 10) : 2;
    if (!threads || threads > 16)
        return 2;
    pthread_t workers[16];
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    if (getenv("MALI_G2_REPLAY_STACK_KB") &&
        pthread_attr_setstacksize(&attributes,
            strtoul(getenv("MALI_G2_REPLAY_STACK_KB"), NULL, 10) * 1024))
        return 2;
    glsl_type_singleton_init_or_ref();
    for (unsigned i = 0; i < threads; i++)
        if (pthread_create(&workers[i], &attributes, run_batch, &batch))
            return 1;
    for (unsigned i = 0; i < threads; i++)
        pthread_join(workers[i], NULL);
    pthread_attr_destroy(&attributes);
    printf("REPLAY_BATCH_%s passed=%zu/%zu threads=%u\n",
           batch.failure ? "FAIL" : "PASS", batch.passed, batch.count, threads);
    for (size_t i = 0; i < batch.count; i++)
        free(batch.paths[i]);
    free(batch.paths);
    glsl_type_singleton_decref();
    return batch.failure;
}
