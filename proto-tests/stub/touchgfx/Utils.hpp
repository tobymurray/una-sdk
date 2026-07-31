// Minimal stand-in for TouchGFX's touchgfx_printf, so the logger and the
// simulator sink can be compiled and run without the TouchGFX/SDL2 tree.
// Carries the format attribute the real one should also carry.
#pragma once

#include <cstdarg>
#include <cstdio>

__attribute__((format(printf, 1, 2)))
static inline void touchgfx_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
