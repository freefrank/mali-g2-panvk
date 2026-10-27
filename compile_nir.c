#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "compiler/nir/nir_builder.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/model/pan_model.h"
#include "util/u_dynarray.h"

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    unsigned arch = 14;
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
    nir_def *value = nir_iadd_imm(&b, nir_imul(&b, i, i), 7);
    nir_store_global(&b, value, address, .align_mul = 4);
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
    util_dynarray_fini(&binary);
    ralloc_free(b.shader);
    return failed;
}
