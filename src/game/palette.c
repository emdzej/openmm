/* The palettes (docs/re/functions.md, "pal"): the 768-byte palette files (6-bit RGB, 256 colours) read
   into the work segment 7d78:0000, and the fades. A fade out reads the DAC back into 7d78:0000 and
   lowers every value by one on every second of 126 steps; a fade in runs a working copy at 7d78:0300 up
   from black towards 7d78:0000 in 128 steps. Every step writes all 256 colours to the DAC, with no wait
   for the retrace: the speed of a fade is the CPU's (in the port, instant). The flag 193c:26ce pairs
   them: a fade out only from a faded-in screen, a fade in only after a fade out. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

#include "segs.h"
enum { SEG_WORK = SEG_BACK };   /* the work segment */

/* The flags of `cmp a, b` (byte). */
static void cmp8(uint8_t a, uint8_t b)
{
    R.cf = a < b;
    flags_zs8((uint8_t)(a - b));
}

/* Pal_LoadFile 1000:07a0
   Opens the file named at DS:DX (no error check: the handle is whatever AX holds), reads 300h bytes into
   7d78:0000 and closes it. It does not set the DAC: the block-set's function number (1012h) is loaded
   and then overwritten byte by byte before the int, so the call made is AX = 1001h, the overscan
   (border) colour, BH = 0Fh (as the original; Pal_LoadIntro sets the DAC after it). Then the text
   window 0,0-25,40 is cleared (int 10h 06h, attribute 0Fh). All registers kept (pusha, ds, es). */
void Pal_LoadFile(void)
{
    Cpu saved = R;
    R.ax = 0x3d00;
    mach_int(0x21);
    R.bx = SEG_WORK;
    R.ds = R.bx;
    R.bx = R.ax;
    SET_AH(0x3f);
    R.dx = 0;
    R.cx = 0x300;
    mach_int(0x21);
    SET_AH(0x3e);
    mach_int(0x21);
    R.ax = SEG_WORK;
    R.es = R.ax;
    R.ax = 0x1012;
    R.bx = 0;
    R.cx = 0x100;
    R.dx = 0;
    SET_AL(1);
    SET_AH(0x10);
    SET_BH(0x0f);
    mach_int(0x10);
    R.ax = 0x0600;
    SET_BH(0x0f);
    R.cx = 0;
    SET_DH(0x19);
    SET_DL(0x28);
    mach_int(0x10);
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    uint16_t sp = R.sp, ss = R.ss;
    R = saved;
    R.sp = sp, R.ss = ss;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Pal_LoadIntro 1000:26ec
   INTRO.PAL (the name at 193c:0d62) into 7d78:0000 through Pal_LoadFile, then all 256 DAC registers set
   from it (int 10h 1012h). ES kept. */
void Pal_LoadIntro(void)
{
    R.dx = 0x0d62;
    Pal_LoadFile();
    uint16_t es = R.es;
    R.ax = SEG_WORK;
    R.es = R.ax;
    R.ax = 0x1012;
    R.bx = 0;
    R.cx = 0x100;
    R.dx = 0;
    mach_int(0x10);
    R.es = es;
}

/* The tail Pal_FadeOutStep shares with Pal_FadeInStep (1000:331f): 256 colours from DS:SI to the DAC,
   from colour 0; then CX back as the step pushed it. */
static void fade_write_dac(uint16_t cx)
{
    R.dx = 0x3c8;
    SET_AL(0);
    R.cf = false;
    flags_zs8(0);
    io_out8(R.dx, AL);
    R.dx++;
    flags_zs16(R.dx);
    R.cx = 0x100;
    do {
        for (int k = 0; k < 3; k++) {
            SET_AL(mb(R.ds, R.si));
            R.si = (uint16_t)(R.df ? R.si - 1 : R.si + 1);
            io_out8(R.dx, AL);
        }
    } while (--R.cx);
    R.cx = cx;
}

/* Pal_FadeOutStep 1000:32ae
   One step of the fade out, CX = the step: on an even step every nonzero byte of 7d78:0000..02ff is
   lowered by one (ES = 7d78, DI left at 300h); then the palette at DS:0000 (DS = 7d78 from the caller)
   is written to the DAC. CX kept. */
void Pal_FadeOutStep(void)
{
    uint16_t cx = R.cx;
    R.cf = R.cx & 1;
    R.cx >>= 1;
    if (!R.cf) {
        R.ax = SEG_WORK;
        R.es = R.ax;
        R.di = 0;
        R.cx = 0x300;
        do {
            uint8_t v = mb(R.es, R.di);
            if (v != 0) mbw(R.es, R.di, (uint8_t)(v - 1));
            R.di++;
        } while (--R.cx);
    }
    R.si = 0;
    fade_write_dac(cx);
}

/* Pal_FadeInStep 1000:3303
   One step of the fade in, CX = the step, DS = 7d78: on an even step every byte of the working palette
   (DS:0300..05ff) that differs from its target (DS:0000..02ff) is raised by one (a "differs", not a
   "below": a working value above its target would climb on and wrap; from black it cannot happen).
   Then the working palette goes to the DAC. CX kept; BL = the last target byte compared. */
void Pal_FadeInStep(void)
{
    uint16_t cx = R.cx;
    R.cf = R.cx & 1;
    R.cx >>= 1;
    if (!R.cf) {
        R.di = 0;
        R.cx = 0x300;
        do {
            SET_BL(mb(R.ds, R.di));
            uint16_t w = (uint16_t)(R.di + 0x300);
            if (BL != mb(R.ds, w)) mbw(R.ds, w, (uint8_t)(mb(R.ds, w) + 1));
            R.di++;
        } while (--R.cx);
    }
    R.si = 0x300;
    fade_write_dac(cx);
}

/* Pal_FadeOut 1000:327a
   Unless already faded ([26ce] = 1), marks it, reads the DAC into 7d78:0000 (int 10h 1017h) and runs
   the steps 126 down to 1 of Pal_FadeOutStep: 63 lowerings, so every colour reaches black. All
   registers kept (pusha, es, ds). */
void Pal_FadeOut(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    cmp8(mb(R.ds, 0x26ce), 1);
    if (!R.zf) {
        mbw(R.ds, 0x26ce, 1);
        R.ax = SEG_WORK;
        R.es = R.ax;
        R.ds = R.ax;
        R.dx = 0;
        R.ax = 0x1017;
        R.bx = 0;
        R.cx = 0x100;
        mach_int(0x10);
        R.cx = 0x7e;
        do Pal_FadeOutStep();
        while (--R.cx);
    }
    bool cf = R.cf, zf = R.zf, sf = R.sf, df = R.df;
    uint16_t sp = R.sp, ss = R.ss;
    R = saved;
    R.sp = sp, R.ss = ss;
    R.cf = cf, R.zf = zf, R.sf = sf, R.df = df;
}

/* Pal_FadeIn 1000:32ce
   Only after a fade out ([26ce] = 1, cleared here): the working palette 7d78:0300..05ff zeroed, then
   the steps 0 to 127 of Pal_FadeInStep towards the palette at 7d78:0000 (64 raisings: any 6-bit value
   is reached). All registers kept (pusha, es, ds). */
void Pal_FadeIn(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    cmp8(mb(R.ds, 0x26ce), 1);
    if (R.zf) {
        mbw(R.ds, 0x26ce, 0);
        R.ax = SEG_WORK;
        R.es = R.ax;
        R.ds = R.ax;
        R.di = 0x300;
        R.si = 0;
        R.cx = 0x180;
        R.ax = 0;
        while (R.cx) {   /* rep stosw */
            mww(R.es, R.di, R.ax);
            R.di = (uint16_t)(R.df ? R.di - 2 : R.di + 2);
            R.cx--;
        }
        do {
            Pal_FadeInStep();
            R.cx++;
            R.cf = R.cx < 0x80;
            flags_zs16((uint16_t)(R.cx - 0x80));
        } while (R.cx != 0x80);
    }
    bool cf = R.cf, zf = R.zf, sf = R.sf, df = R.df;
    uint16_t sp = R.sp, ss = R.ss;
    R = saved;
    R.sp = sp, R.ss = ss;
    R.cf = cf, R.zf = zf, R.sf = sf, R.df = df;
}

/* Pal_LoadRound 1000:4758
   The race's palette ROUNDn.PAL (the name at 193c:1200, patched by the track's set-up) into 7d78:0000.
   On theme 3 ([28bf]) the three colours 251-253 are replaced by 219-221 on track 2 or by 235-237 on
   track 1 ([28c0]). CF = 1 if the open or the read fails (the file is closed even then, with whatever
   BX holds). All registers kept. */
void Pal_LoadRound(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.dx = 0x1200;
    R.ax = 0x3dc0;
    mach_int(0x21);
    R.bx = R.ax;
    bool ok = !R.cf;
    if (ok) {
        R.dx = 0;
        R.cx = 0x300;
        R.ax = SEG_WORK;
        R.ds = R.ax;
        SET_AH(0x3f);
        mach_int(0x21);
        ok = !R.cf;
    }
    if (ok) {
        SET_AH(0x3e);
        mach_int(0x21);
        R.ax = SEG_DATA;
        R.ds = R.ax;
        cmp8(mb(R.ds, 0x28bf), 3);
        if (R.zf) {
            uint16_t from = 0;
            cmp8(mb(R.ds, 0x28c0), 2);
            if (R.zf) from = 0x291;
            else {
                cmp8(mb(R.ds, 0x28c0), 1);
                if (R.zf) from = 0x2c1;
            }
            if (from) {
                R.ax = SEG_WORK;
                R.ds = R.ax;
                for (uint16_t i = 0; i < 9; i++) mbw(R.ds, (uint16_t)(0x2f1 + i), mb(R.ds, (uint16_t)(from + i)));
            }
        }
        R.cf = false;
    } else {
        SET_AH(0x3e);
        mach_int(0x21);
        R.cf = true;
    }
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    uint16_t sp = R.sp, ss = R.ss, bp = R.bp;
    R = saved;
    R.sp = sp, R.ss = ss, R.bp = bp;
    R.cf = cf, R.zf = zf, R.sf = sf;
}
