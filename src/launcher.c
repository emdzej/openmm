/* MICRO.COM, the launcher the player types (docs/re/launcher.md), ported: it shrinks its memory, has
   DOS run SM.EXE (int 21h 4B00h), then MICRO.EXE; if either cannot be run, it sets text mode and
   prints why (its messages are read from the user's MICRO.COM), and ends.

   In the port DOS's EXEC is this file: each program is loaded at 1000:0000 with its PSP at 0FF0 (as the
   harness loads them; on the real machine they sit above MICRO.COM, which the relocations make
   invisible), the rest of memory left as the previous program left it, and its ported entry run until
   it ends (int 21h 4Ch, dos.c). SM.EXE is a plain MZ program; MICRO.EXE's PKLITE stub is ported in
   pklite.c and its unpacked image checked in exe.c. */
#include "launcher.h"
#include "cpu.h"
#include "exe.h"
#include "fntab.h"
#include "machine.h"
#include "mem.h"
#include "platform.h"
#include "routines.h"
#include "sm/sm.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dos_set_program_end(uint16_t seg);
bool dos_exited(uint8_t *code);

/* The MZ header's words. */
static uint16_t hw(const uint8_t *d, int i) { return (uint16_t)(d[2 * i] | d[2 * i + 1] << 8); }

/* The end of a program's block as DOS sizes it: the image and up to maxalloc more paragraphs (FFFFh:
   all memory, to A000h). */
static uint16_t block_end(uint32_t image_bytes, uint16_t maxalloc)
{
    uint32_t end = SEG_CODE + (image_bytes + 15) / 16 + (uint32_t)maxalloc;
    return maxalloc == 0xffff || end > 0xa000 ? 0xa000 : (uint16_t)end;
}

static void start_regs(uint16_t ss, uint16_t sp)
{
    memset(&R, 0, sizeof R);
    R.ds = R.es = SEG_PSP;
    R.ss = ss, R.sp = sp;
    R.fl = 0x0202;
}

/* SM.EXE: loaded from the user's file, run to its end. False if it cannot be run. */
static bool run_sm(void)
{
    size_t n;
    uint8_t *d = vfs_read_all("SM.EXE", &n);
    if (!d || n < 0x20 || d[0] != 'M' || d[1] != 'Z') { free(d); return false; }
    uint32_t size = (uint32_t)hw(d, 2) * 512 - (hw(d, 1) ? 512 - hw(d, 1) : 0), hdr = (uint32_t)hw(d, 4) * 16;
    if (size > n || hdr > size) { free(d); return false; }
    int nrel = hw(d, 3);
    uint32_t *rel = malloc((size_t)(nrel ? nrel : 1) * sizeof *rel);
    for (int i = 0; i < nrel; i++) {
        uint32_t at = hw(d, 0x0c) + 4u * (uint32_t)i;
        if (at + 4 > n) { free(rel), free(d); return false; }
        rel[i] = (uint32_t)(d[at] | d[at + 1] << 8) + 16u * (uint32_t)(d[at + 2] | d[at + 3] << 8);
    }
    mem_place_image(d + hdr, size - hdr, rel, nrel, "SM.EXE");
    dos_set_program_end(block_end(size - hdr, hw(d, 6)));
    start_regs((uint16_t)(SEG_CODE + hw(d, 7)), hw(d, 8));
    static const FnEntry sm_tab[] = {{0x10000cbfu, "Sm_KeyIrq", Sm_KeyIrq, 1}};
    mach_set_program(sm_tab, 1);
    Sm_Start();
    mach_set_program(NULL, 0);
    dos_exited(NULL);
    free(rel), free(d);
    return true;
}

/* MICRO.EXE: its unpacked image (exe.c) placed as its stub leaves it, run to its end. */
static uint8_t run_micro(void)
{
    size_t n;
    const uint8_t *img = exe_image(&n);
    const ExeEntry *e = exe_entry();
    mem_place_image(img, n, e->reloc, e->nreloc, "MICRO.EXE");
    mww(SEG_PSP, 0x5c, 0x4b50);   /* "PK", which the PKLITE stub writes */
    dos_set_program_end(0xa000);  /* its header asks for all memory */
    start_regs(e->ss, e->sp);
    Start();
    uint8_t code = 0;
    dos_exited(&code);
    return code;
}

/* MICRO.COM's message at its offset (100h-based), printed in text mode with the BIOS's teletype. */
static void com_message(const uint8_t *com, size_t n, uint16_t off, char extra)
{
    char m[128];
    size_t k = 0;
    for (size_t i = (size_t)(off - 0x100); i < n && com[i] && k + 2 < sizeof m; i++) {
        char c = (char)com[i];
        if (c == '\r') continue;
        if (c == '\n') c = ' ';
        m[k++] = c;
    }
    if (extra) m[k++] = extra;
    m[k] = 0;
    plat_log(m);
}

/* Launcher_Main MICRO:0100 */
int Launcher_Main(void)
{
    size_t n = 0;
    uint8_t *com = vfs_read_all("MICRO.COM", &n);
    mem_init_machine();
    mach_reset();
    /* SM.EXE first: a failure ends it all with "cannot find" (error 2) or "not enough memory" */
    bool skip_sm = false;
    char v[8];
    if (plat_param("splash", v, sizeof v) && v[0] == '0') skip_sm = true;   /* opt-in: no splash */
    if (!skip_sm && !run_sm()) {
        if (com) com_message(com, n, 0x25d, 0);
        free(com);
        return 1;
    }
    uint8_t code = run_micro();
    free(com);
    return code;
}
