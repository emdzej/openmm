/* Every routine of the original by its address (generated: tools/gen_routines.py), for the interrupt
   vectors the program installs and for the checker (tools/fncheck.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct FnEntry_ {
    uint32_t addr;   /* seg:off packed 0xSSSSOOOO */
    const char *name;
    void (*fn)(void);
    int ported;      /* 0: the PENDING_NOTE stand-in */
} FnEntry;

extern const FnEntry g_fntab[];
extern const int g_fntab_n;

const FnEntry *fntab_find(uint32_t addr);
const FnEntry *fntab_by_name(const char *name);
