#ifndef PANVK_G2_TRACE_H
#define PANVK_G2_TRACE_H

#include <stdbool.h>
#include <stdint.h>

void panvk_g2_trace(const char *format, ...);
void panfrost_valhall_compile_error(const char *reason, const char *instruction);

static inline bool
panvk_g2_trace_submission(uint32_t id)
{
   return id <= 32 || id % 128 == 0;
}

#endif
