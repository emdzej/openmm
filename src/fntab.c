/* Lookups in the generated table of routines (fntab.h). */
#include "fntab.h"
#include <string.h>

const FnEntry *fntab_find(uint32_t addr)
{
    for (int i = 0; i < g_fntab_n; i++)
        if (g_fntab[i].addr == addr) return &g_fntab[i];
    return NULL;
}

const FnEntry *fntab_by_name(const char *name)
{
    for (int i = 0; i < g_fntab_n; i++)
        if (!strcmp(g_fntab[i].name, name)) return &g_fntab[i];
    return NULL;
}
