/* Shared by the ported routines (docs/re/porting.md). */
#pragma once
#include <stdbool.h>

/* Main_Restart 1000:00cc: set when the original reloads Start's saved SS:SP and jumps back into its loop;
   the routines on the way back return at once (CALL_R) until Start takes it. */
extern bool g_restart;
#define CALL_R(f)                      \
    do {                               \
        f();                           \
        if (g_restart) return;         \
    } while (0)
