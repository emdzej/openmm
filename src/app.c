/* The application (app.h): the exe found, checked and unpacked, then loaded as DOS loads it (mem.c), the
   machine reset (machine.c) and the program run from its entry, Start (1000:0006). MICRO.COM's order
   (SM.EXE first) comes with step 4 of docs/re/port-plan.md. */
#include "app.h"
#include "cpu.h"
#include "exe.h"
#include "machine.h"
#include "mem.h"
#include "platform.h"
#include "routines.h"
#include <stdio.h>
#include <string.h>

bool app_init(void)
{
    char err[256], m[320];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenMM: %s", err);
        plat_log(m);
        return false;
    }
    const ExeEntry *e = exe_entry();
    snprintf(m, sizeof m, "OpenMM: MICRO.EXE (1994 floppy release) found; unpacked %u bytes, entry %04x:%04x, "
             "stack %04x:%04x, %d relocations", EXE_IMAGE_SIZE, e->cs, e->ip, e->ss, e->sp, e->nreloc);
    plat_log(m);
    return true;
}

int app_main(void)
{
    mem_load_program();
    mach_reset();
    const ExeEntry *e = exe_entry();
    memset(&R, 0, sizeof R);
    R.ds = R.es = SEG_PSP, R.ss = e->ss, R.sp = e->sp;
    Start();
    plat_exit(0);   /* not reached: the program ends through int 21h 4Ch */
    return 0;
}

void app_audio(float *stereo, unsigned frames) { memset(stereo, 0, (size_t)frames * 2 * sizeof *stereo); }

void app_exit(void) { exe_free(); }
