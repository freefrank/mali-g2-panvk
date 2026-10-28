#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/nir/nir_builder.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/model/pan_model.h"
#include "util/u_dynarray.h"

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    unsigned arch = argc > 2 ? strtoul(argv[2], NULL, 0) : 14;
    const nir_shader_compiler_options *options =
        pan_get_nir_shader_compiler_options(arch, MESA_SHADER_COMPUTE, false);
    nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options,
                                                   "g2_compute_bringup");
    b.shader->info.workgroup_size[0] = 32;
    b.shader->info.workgroup_size[1] = 1;
    b.shader->info.workgroup_size[2] = 1;
    nir_def *i = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
    nir_def *base = nir_load_push_constant(&b, 1, 64, nir_imm_int(&b, 0));
    nir_def *address = nir_iadd(&b, base, nir_ishl_imm(&b, nir_u2u64(&b, i), 2));
    const char *mode = argc > 3 ? argv[3] : "integer";
    if (!strcmp(mode, "loop")) {
        nir_function_impl *impl = nir_shader_get_entrypoint(b.shader);
        nir_variable *counter = nir_local_variable_create(impl, glsl_uint_type(), "counter");
        nir_variable *sum = nir_local_variable_create(impl, glsl_uint_type(), "sum");
        nir_store_var(&b, counter, nir_imm_int(&b, 0), 1);
        nir_store_var(&b, sum, nir_imm_int(&b, 7), 1);
        nir_loop *loop = nir_push_loop(&b);
        nir_def *c = nir_load_var(&b, counter);
        nir_if *stop = nir_push_if(&b, nir_uge(&b, c, i));
        nir_jump(&b, nir_jump_break);
        nir_pop_if(&b, stop);
        nir_def *odd = nir_iadd_imm(&b, nir_ishl_imm(&b, c, 1), 1);
        nir_store_var(&b, sum, nir_iadd(&b, nir_load_var(&b, sum), odd), 1);
        nir_store_var(&b, counter, nir_iadd_imm(&b, c, 1), 1);
        nir_pop_loop(&b, loop);
        nir_store_global(&b, nir_load_var(&b, sum), address, .align_mul = 4);
    } else if (!strcmp(mode, "branch")) {
        nir_if *conditional = nir_push_if(&b, nir_ult_imm(&b, i, 16));
        nir_store_global(&b, nir_iadd_imm(&b, nir_imul(&b, i, i), 7), address, .align_mul = 4);
        nir_push_else(&b, conditional);
        nir_def *value = nir_imul(&b, nir_iadd_imm(&b, i, 1), nir_iadd_imm(&b, i, -1));
        nir_store_global(&b, nir_iadd_imm(&b, value, 8), address, .align_mul = 4);
        nir_pop_if(&b, conditional);
    } else {
        nir_def *value;
        if (!strcmp(mode, "float")) {
            nir_def *x = nir_u2f32(&b, i);
            value = nir_f2u32(&b, nir_fadd_imm(&b, nir_fmul(&b, x, x), 7));
        } else {
            value = nir_iadd_imm(&b, nir_imul(&b, i, i), 7);
        }
        nir_store_global(&b, value, address, .align_mul = 4);
        if (strstr(mode, "local_") == mode || strstr(mode, "group_") == mode ||
            strstr(mode, "global_") == mode) {
            unsigned component = mode[strlen(mode) - 1] - 'x';
            nir_def *id;
            if (mode[0] == 'l') id = nir_load_local_invocation_id(&b);
            else if (mode[1] == 'r') id = nir_load_workgroup_id(&b);
            else id = nir_load_global_invocation_id(&b, 32);
            nir_store_global(&b, nir_channel(&b, id, component),
                             nir_iadd_imm(&b, address, 128), .align_mul = 4);
        }
    }
    NIR_PASS(_, b.shader, nir_lower_vars_to_ssa);
    nir_validate_shader(b.shader, "before G2 bringup compile");
    struct pan_compile_inputs inputs = {.arch = arch, .fau = {.reserved = 2}};
    struct pan_shader_info info = {0};
    struct util_dynarray binary = UTIL_DYNARRAY_INIT;
    pan_postprocess_nir(b.shader, &inputs, &info);
    pan_shader_compile(b.shader, &inputs, &binary, &info);
    FILE *output = fopen(argv[1], "wb");
    if (!output) { perror(argv[1]); return 1; }
    size_t written = fwrite(binary.data, 1, binary.size, output);
    int failed = written != binary.size || fclose(output);
    printf("arch=%u binary_bytes=%u preload=%#" PRIx64 " work_regs=%u fau=%u\n",
           arch, binary.size, info.preload, info.work_reg_count, info.fau.count);
    char metadata_path[2048];
    snprintf(metadata_path, sizeof(metadata_path), "%s.json", argv[1]);
    FILE *metadata = fopen(metadata_path, "w");
    if (metadata) {
        fprintf(metadata, "{\"arch\":%u,\"bytes\":%u,\"preload\":%" PRIu64
                ",\"work_registers\":%u,\"mode\":\"%s\"}\n",
                arch, binary.size, info.preload, info.work_reg_count, mode);
        fclose(metadata);
    }
    util_dynarray_fini(&binary);
    ralloc_free(b.shader);
    return failed;
}
