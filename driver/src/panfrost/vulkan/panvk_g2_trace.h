#ifndef PANVK_G2_TRACE_H
#define PANVK_G2_TRACE_H

#include <stdbool.h>
#include <stdint.h>

struct nir_shader;
struct pan_compile_inputs;
struct pan_shader_info;

void panvk_g2_trace(const char *format, ...);
void panvk_g2_check_range(uint64_t addr, uint64_t size, const char *kind, uint32_t binding,
                          uint32_t elem);
void panvk_g2_check_install(void);

static inline uint64_t
panvk_g2_checked_ptr(uint64_t addr, uint64_t size, const char *kind)
{
   panvk_g2_check_range(addr, size, kind, 0, 0);
   return addr;
}
void panfrost_valhall_compile_error(const char *reason, const char *instruction);
void panvk_g2_trace_shader(uint32_t id, uint32_t point,
                           const struct nir_shader *nir,
                           const struct pan_compile_inputs *inputs,
                           const struct pan_shader_info *info);

static inline bool
panvk_g2_trace_submission(uint32_t id)
{
   return id <= 32 || id % 128 == 0;
}

#endif
