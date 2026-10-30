#ifndef PANVK_G2_TRACE_H
#define PANVK_G2_TRACE_H

#include <stdbool.h>

extern bool panvk_g2_diagnostics;

void panvk_g2_trace(const char *format, ...);
void panfrost_valhall_compile_error(const char *reason, const char *instruction);

#endif
