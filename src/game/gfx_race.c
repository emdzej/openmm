/* The race's blits (docs/re/drawing.md). The race's back buffer (6d78, rows of 110h) has a 16-pixel
   border, the window at 1110h, and scrolls by whole tiles: the fine scroll, 0..f pixels right and down
   as one offset y * 110h + x, is cs:8994 (set by the race's frame, 1000:90c5), subtracted from every
   destination. Sprites are packed rows (width bytes each), colour 0 transparent. */
#include "copy.h"
#include "cpu.h"
#include "gfx.h"
#include "mem.h"
#include "routines.h"

/* `cmp a, b` as the rejecting compares leave the flags */
static void cmp16(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    flags_zs16((uint16_t)(a - b));
}

/* Gfx_ClipSprite 1000:8bab
   DI = x, AX = y, CX = width, DX = height, SI = the sprite: clips the rectangle to the 256 x 224 window
   (x 0..ffh, y 0..dfh). Nothing left: CF set (AX, CX, DX, SI, DI as far as it got; BX, BP kept). Else
   CF clear, DI = the back-buffer offset (row * 110h + x + 1110h - cs:8994), SI moved past what is cut
   off on the left and the top, CL = the columns, CH = AL = the rows (AX the rows), DX = the source
   skip per row; BX, BP kept.
   As the original: a sprite cut at the bottom gets the offset of row 200 + x whatever its y (the
   bottom rows of the 224 are below the 200 presented, so such a sprite lands off the picture). */
void Gfx_ClipSprite(void)
{
    uint16_t w = R.cx, h = R.dx, y = R.ax;
    R.dx = 0;
    if (R.di & 0x8000) {
        int32_t s = (int16_t)R.cx + (int16_t)R.di;
        R.cx = (uint16_t)s;
        if (s <= 0) goto off;
        R.si = (uint16_t)(R.si - R.di);
        R.dx = (uint16_t)(R.dx - R.di);
        R.di = 0;
    } else {
        if (R.di >= 0x100) goto off;
        R.di = (uint16_t)(R.di + R.cx);
        if (R.di > 0x100) {
            R.ax = R.di;
            R.di = (uint16_t)(R.di - R.cx);
            R.ax = (uint16_t)(R.ax - 0x100);
            R.cx = (uint16_t)(R.cx - R.ax);
            R.dx = (uint16_t)(R.dx + R.ax);
        } else {
            R.di = (uint16_t)(R.di - R.cx);
        }
    }
    uint16_t cols = R.cx, skip = R.dx;
    R.ax = h;
    R.cx = y;
    if (R.cx & 0x8000) {
        int32_t s = (int16_t)R.cx + (int16_t)R.ax;
        R.cx = (uint16_t)s;
        if (s <= 0) goto off;
        R.ax = (uint16_t)(R.ax - R.cx);
        R.si = (uint16_t)(R.si + (uint16_t)((uint32_t)R.ax * w));
        R.ax = R.cx;
        R.di = (uint16_t)(R.di + 0x1110);
    } else {
        if (R.cx >= 0xe0) goto off;
        R.cx = (uint16_t)(R.cx + R.ax);
        if (R.cx > 0xe0) {
            R.cx = (uint16_t)(R.cx - R.ax);
            R.ax = (uint16_t)(0xe0 - R.cx);
            R.di = (uint16_t)(R.di + 0xe590);   /* 200 * 110h + 1110h */
        } else {
            R.cx = (uint16_t)(R.cx - R.ax);
            R.di = (uint16_t)(R.di + (uint16_t)(R.cx * 0x110u) + 0x1110);
        }
    }
    R.di = (uint16_t)(R.di - c16(0x8994));
    R.dx = skip;
    R.cx = (uint16_t)(AL << 8 | (cols & 0xff));
    R.cf = false;
    return;
off:
    R.cf = true;
}

/* Gfx_BlitSilhouette 1000:8c3c
   After Gfx_ClipSprite (CL columns, CH rows, DX skip, DI, SI; ES = the back buffer): the sprite's
   non-zero pixels in colour 0 (the shadows; the banners' blink). Returns AX = 0, CX = columns << 8,
   DX the buffer's skip, BP the source skip; CF from the last row's step. */
void Gfx_BlitSilhouette(void)
{
    R.bp = R.dx;
    uint8_t cols = CL, rows = CH;
    R.dx = (uint16_t)(0x110 - cols);
    do {
        uint8_t n = cols;
        do {
            if (lodsb()) stosb(0);
            else R.di++;
        } while (--n);
        R.si = (uint16_t)(R.si + R.bp);
        R.di = add16(R.di, R.dx);
    } while (--rows);
    R.ax = 0;
    R.cx = (uint16_t)(cols << 8);
    R.zf = true, R.sf = false;
}

/* Gfx_BlitRecolour 1000:8c6a
   After Gfx_ClipSprite, AL = a colour offset: the sprite's non-zero pixels, those whose low nibble is
   0..2 moved by AL (the vehicles' team colours). BX kept; AL the last pixel drawn (0 if the last was
   transparent), AH the last drawn pixel's low nibble, CX = 0, DX the buffer's skip, BP the source
   skip; CF from the last row's step. */
void Gfx_BlitRecolour(void)
{
    R.bp = R.dx;
    uint8_t cols = CL, offset = AL;
    R.dx = (uint16_t)(0x110 - cols);
    do {
        SET_CL(cols);
        do {
            uint8_t v = lodsb();
            SET_AL(v);
            if (v) {
                SET_AH(v & 0x0f);
                if (AH <= 2) SET_AL(v + offset);
                stosb(AL);
            } else {
                R.di++;
            }
            SET_CL(CL - 1);
        } while (CL);
        R.si = (uint16_t)(R.si + R.bp);
        R.di = add16(R.di, R.dx);
        SET_CH(CH - 1);
    } while (CH);
    R.zf = true, R.sf = false;
}

/* Gfx_BlitSprite 1000:8ca4
   After Gfx_ClipSprite: the sprite's non-zero pixels. BX kept; AL the last pixel read, CX = 0, DL = the
   columns, BP the source skip; CF from the last row's step. */
void Gfx_BlitSprite(void)
{
    uint16_t step = (uint16_t)(0x110 - CL);
    R.bp = R.dx;
    SET_DL(CL);
    do {
        SET_CL(DL);
        do {
            uint8_t v = lodsb();
            SET_AL(v);
            if (v) stosb(v);
            else R.di++;
            SET_CL(CL - 1);
        } while (CL);
        R.si = (uint16_t)(R.si + R.bp);
        R.di = add16(R.di, step);
        SET_CH(CH - 1);
    } while (CH);
    R.zf = true, R.sf = false;
}

/* The bounds test of the unclipped draws: x DI in -17h..100h, y AX in -17h..c8h (signed), the flags as
   the compare that rejects leaves them; true if inside. */
static bool unclipped_in(void)
{
    if ((int16_t)R.di < -0x17) return cmp16(R.di, 0xffe9), false;
    if ((int16_t)R.di > 0x100) return cmp16(R.di, 0x100), false;
    if ((int16_t)R.ax < -0x17) return cmp16(R.ax, 0xffe9), false;
    if ((int16_t)R.ax > 0xc8) return cmp16(R.ax, 0xc8), false;
    return true;
}

/* y * 110h + x + 1110h for the unclipped draws (imul: y signed; AX the low product, CX = 110h) */
static void unclipped_offset(void)
{
    int32_t p = (int16_t)R.ax * 0x110;
    R.ax = (uint16_t)p;
    R.cx = 0x110;
    R.di = (uint16_t)(R.di + R.ax + 0x1110);
}

/* The body of Gfx_BlitAt (from 1000:8d0a): DI = offset before the scroll, DL x DH the packed sprite at
   SI; colour 0 transparent, or every non-zero pixel in colour 0 (1000:8d70). Flags as the last row's
   step leaves them. */
static void blit_at(bool black)
{
    R.di = (uint16_t)(R.di - c16(0x8994));
    uint8_t w = DL, rows = DH;
    uint16_t step = (uint16_t)(0x110 - w);
    do {
        uint8_t n = w;
        do {
            uint8_t v = lodsb();
            if (v) stosb(black ? 0 : v);
            else R.di++;
        } while (--n);
        R.di = add16(R.di, step);
    } while (--rows);
    R.zf = true, R.sf = false;
}

/* R back as saved, the flags as left (the pusha/popa routines) */
static void restore_keep_flags(const Cpu *saved)
{
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    R = *saved;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Gfx_DrawSmall8 1000:8cd0
   CX = frame, SI = an 8 x 8 sheet (40h a frame), DI/AX = the centre on screen: Gfx_DrawUnclipped at
   x - 4, y - 4. Registers kept (flags from the draw). */
void Gfx_DrawSmall8(void)
{
    Cpu saved = R;
    R.ax = (uint16_t)(R.ax - 4);
    R.di = (uint16_t)(R.di - 4);
    R.cx = (uint16_t)(R.cx << 6);
    R.si = (uint16_t)(R.si + R.cx);
    R.dx = 0x808;
    Gfx_DrawUnclipped();
    restore_keep_flags(&saved);
}

/* Gfx_DrawUnclipped 1000:8ce4
   DI = x, AX = y, DL = width, DH = height, SI = the packed sprite: dropped whole if x is outside
   -17h..100h or y outside -17h..c8h, else drawn without clipping, colour 0 transparent. As the
   original: the range is wider than the 16-pixel border, so a sprite at x < -16 spills into the end of
   the row above. Registers kept. */
void Gfx_DrawUnclipped(void)
{
    Cpu saved = R;
    if (unclipped_in()) {
        unclipped_offset();
        R.dx = saved.dx;
        blit_at(false);
    }
    restore_keep_flags(&saved);
}

/* Gfx_BlitAt 1000:8d09
   DI = back-buffer offset (before the scroll is taken off), DL = width, DH = height, SI = the packed
   sprite: drawn with colour 0 transparent (the HUD's digits and icons). Registers kept. */
void Gfx_BlitAt(void)
{
    Cpu saved = R;
    blit_at(false);
    restore_keep_flags(&saved);
}

/* Gfx_DrawSmall8Black 1000:8d36
   As Gfx_DrawSmall8, in colour 0 (the shells' shadows). Registers kept. */
void Gfx_DrawSmall8Black(void)
{
    Cpu saved = R;
    R.ax = (uint16_t)(R.ax - 4);
    R.di = (uint16_t)(R.di - 4);
    R.cx = (uint16_t)(R.cx << 6);
    R.si = (uint16_t)(R.si + R.cx);
    R.dx = 0x808;
    Gfx_DrawUnclippedBlack();
    restore_keep_flags(&saved);
}

/* Gfx_DrawUnclippedBlack 1000:8d4a
   As Gfx_DrawUnclipped, every non-zero pixel in colour 0. Registers kept. */
void Gfx_DrawUnclippedBlack(void)
{
    Cpu saved = R;
    if (unclipped_in()) {
        unclipped_offset();
        R.dx = saved.dx;
        blit_at(true);
    }
    restore_keep_flags(&saved);
}

/* Dead_DrawUnclippedRecolour 1000:8d9e
   Never reached: the bounds test and the offset of Gfx_DrawUnclipped without its register saves, then
   falls into Gfx_BlitRecolourAt. Kept for the record. */
void Dead_DrawUnclippedRecolour(void)
{
    if (!unclipped_in()) return;
    uint16_t dx = R.dx;
    unclipped_offset();
    R.dx = dx;
    Gfx_BlitRecolourAt();
}

/* Gfx_BlitRecolourAt 1000:8dc0
   DI = back-buffer offset (before the scroll), DL = width, DH = height, BL = a colour offset, SI = the
   packed sprite: non-zero pixels drawn, those whose low nibble is 1 or 2 moved by BL. No registers
   kept: BH = the width, CX = 0, DX the buffer's skip, AL the last pixel drawn (0 if the last was
   transparent), AH the last drawn pixel's low nibble; CF from the last row's step. */
void Gfx_BlitRecolourAt(void)
{
    SET_BH(DL);
    R.di = (uint16_t)(R.di - c16(0x8994));
    SET_CL(DH);
    R.dx = (uint16_t)(0x110 - DL);
    do {
        SET_CH(BH);
        do {
            uint8_t v = lodsb();
            SET_AL(v);
            if (v) {
                SET_AH(v & 0x0f);
                if (AH == 1 || AH == 2) SET_AL(v + BL);
                stosb(AL);
            } else {
                R.di++;
            }
            SET_CH(CH - 1);
        } while (CH);
        R.di = add16(R.di, R.dx);
        SET_CL(CL - 1);
    } while (CL);
    R.zf = true, R.sf = false;
}

/* Gfx_DrawBanner 1000:9289
   SI = a banner in DS 193c, DI/AX = its centre on screen: an 88 x 22 sprite (21 rows for the one at
   193c:9d63) centred and clipped, drawn plain, or as a silhouette in colour 0 while the blink flag
   193c:26cf (toggled every 32 ticks by the timer) is 1. Registers kept (flags from the draw). */
void Gfx_DrawBanner(void)
{
    Cpu saved = R;
    R.ds = SEG_DATA;
    R.di = (uint16_t)(R.di - 0x2c);
    R.ax = (uint16_t)(R.ax - 0x0c);
    R.dx = R.si == 0x9d63 ? 0x15 : 0x16;
    R.cx = 0x58;
    Gfx_ClipSprite();
    if (!R.cf) {
        if (d8(0x26cf) == 1) Gfx_BlitSilhouette();
        else Gfx_BlitSprite();
    }
    restore_keep_flags(&saved);
}

/* Gfx_PresentScrolled 1000:92bc
   The race's present: 200 rows of 256 bytes from 6d78:1110h - cs:8994 (stride 110h) to A000:0020
   (stride 140h). The row copy is the code at 1000:92d8..92dd, which Copy_Patch32 rewrites from 80h
   movsw to 40h movsd: it is run from the code segment's bytes (copy.c). DS, ES kept; AX = 6d78h,
   BL = 0, CX = 0, SI and DI past the copy; CF from the last row's step. */
void Gfx_PresentScrolled(void)
{
    uint16_t ds = R.ds, es = R.es;
    R.es = SEG_VGA;
    R.ax = SEG_BACK;
    R.ds = SEG_BACK;
    R.si = (uint16_t)(0x1110 - c16(0x8994));
    R.di = 0x20;
    SET_BL(0xc8);
    do {
        copy_run_code(0x92d8, 0x92de);
        R.si = (uint16_t)(R.si + 0x10);
        R.di = add16(R.di, 0x40);
        SET_BL(BL - 1);
    } while (BL);
    R.zf = true, R.sf = false;
    R.ds = ds, R.es = es;
}
