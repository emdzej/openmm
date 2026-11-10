/* The drawing layer (docs/re/drawing.md): everything is composed in a back buffer at segment 7d78, rows of
   110h bytes, the 256 x 200 picture at offset 888h of the front end (an 8-pixel margin) or 1110h of the
   race (a 16-pixel margin, scrolled by cs:8994), and copied to the VGA's A000 at x 32 once per frame.

   Two families of sprite routines: the front end's sprite objects (a 1bh-byte record in DS: an image,
   a position, a frame, a save-under segment; clipped to the 256 x 200 window), and the race's blits
   (registers only: a rectangle clipped to a 256 x 224 window, then drawn plain, recoloured, or as a
   silhouette). Colour 0 is transparent everywhere except in Gfx_DrawObject. */
#include "cpu.h"
#include "gfx.h"
#include "mem.h"
#include "routines.h"

/* ---- the front end's sprite objects ----

   Object (1bh bytes, DS): +0 image record, +2 x, +4 y (signed), +6 save-under segment (0 none),
   +8 image segment, +0a mirrored (1), +0b frame size, +0d source offset, +0f extent in y, +11 extent
   in x (the image record's +0d and +0f: the row length is +11), +13 frame, +15 back-buffer offset
   (FFFFh: not drawn), +17 source skip per row, +19 rows, +1a columns (the last five set by the clip). */

/* Gfx_InitObject 1000:049c
   BX = object: copies the image record's segment and extents into it, the frame size their product.
   Registers kept; CF as the multiplication leaves it (set if the product needs more than 16 bits). */
void Gfx_InitObject(void)
{
    uint16_t img = mw(R.ds, R.bx);
    uint16_t ey = mw(R.ds, (uint16_t)(img + 0x0d));
    uint16_t ex = mw(R.ds, (uint16_t)(img + 0x0f));
    mww(R.ds, (uint16_t)(R.bx + 8), mw(R.ds, (uint16_t)(img + 0x12)));
    mww(R.ds, (uint16_t)(R.bx + 0x0f), ey);
    mww(R.ds, (uint16_t)(R.bx + 0x11), ex);
    uint32_t p = (uint32_t)ey * ex;
    mww(R.ds, (uint16_t)(R.bx + 0x0b), (uint16_t)p);
    R.cf = p > 0xffff;
}

/* Gfx_DrawObjectSaved 1000:04b8
   BX = object: clipped (nothing when it is off screen), the background under it saved, then drawn with
   colour 0 transparent (falls into Gfx_DrawObjectMasked). */
void Gfx_DrawObjectSaved(void)
{
    Gfx_ClipObject();
    if (R.cf) return;
    Gfx_DrawObjectMasked();
}

/* Gfx_DrawObjectMasked 1000:04bd
   BX = object, already clipped: saves the background (Gfx_SaveUnder), then copies the clipped frame into
   the back buffer skipping colour 0; mirrored ([bx+0a] = 1) it reads each source row backwards from
   its far end. BX, DS, ES kept; AL the last pixel read, AH = CX = the columns, DX the buffer's skip,
   SI, DI past the last row; CF from the last row's step. */
void Gfx_DrawObjectMasked(void)
{
    Gfx_SaveUnder();
    uint16_t obj = R.bx, ds = R.ds, es = R.es;
    R.es = SEG_BACK;
    uint8_t cols = mb(ds, (uint16_t)(obj + 0x1a));
    R.dx = (uint16_t)(0x110 - cols);
    uint8_t rows = mb(ds, (uint16_t)(obj + 0x19));
    R.cx = (uint16_t)(rows << 8 | cols);
    R.di = mw(ds, (uint16_t)(obj + 0x15));
    R.si = mw(ds, (uint16_t)(obj + 0x0d));
    R.ax = mw(ds, (uint16_t)(obj + 8));
    uint8_t mirror = mb(ds, (uint16_t)(obj + 0x0a));
    uint16_t skip = mw(ds, (uint16_t)(obj + 0x17));
    R.ds = R.ax;
    SET_AH(cols);
    uint8_t al = AL;
    if (mirror != 1) {
        R.bx = skip;
        do {
            do {
                al = lodsb();
                if (al) stosb(al);
                else R.di++;
                SET_CL(CL - 1);
            } while (CL);
            SET_CL(AH);
            R.si = (uint16_t)(R.si + R.bx);
            R.di = add16(R.di, R.dx);
            SET_CH(CH - 1);
        } while (CH);
    } else {
        R.bx = (uint16_t)(skip + cols);
        R.si = (uint16_t)(R.si + R.bx);
        do {
            uint16_t row = R.si;
            do {
                R.si--;
                al = mb(R.ds, R.si);
                if (al) stosb(al);
                else R.di++;
                SET_CL(CL - 1);
            } while (CL);
            R.si = row;
            SET_CL(AH);
            R.si = (uint16_t)(R.si + R.bx);
            R.di = add16(R.di, R.dx);
            SET_CH(CH - 1);
        } while (CH);
    }
    SET_AL(al);
    R.zf = true, R.sf = false;
    R.bx = obj, R.ds = ds, R.es = es;
}

/* Gfx_DrawObject 1000:053a
   BX = object: clipped (nothing when off screen: CF set), then copied opaquely, words then the odd
   byte per row; mirrored byte by byte from each row's far end. BX, DS, ES kept; CF from the last row's
   step. */
void Gfx_DrawObject(void)
{
    Gfx_ClipObject();
    if (R.cf) return;
    uint16_t obj = R.bx, ds = R.ds, es = R.es;
    R.es = SEG_BACK;
    uint8_t cols = mb(ds, (uint16_t)(obj + 0x1a));
    R.dx = (uint16_t)(0x110 - cols);
    uint8_t rows = mb(ds, (uint16_t)(obj + 0x19));
    R.cx = (uint16_t)(rows << 8 | cols);
    R.di = mw(ds, (uint16_t)(obj + 0x15));
    R.si = mw(ds, (uint16_t)(obj + 0x0d));
    R.ax = mw(ds, (uint16_t)(obj + 8));
    uint8_t mirror = mb(ds, (uint16_t)(obj + 0x0a));
    uint16_t skip = mw(ds, (uint16_t)(obj + 0x17));
    R.ds = R.ax;
    if (mirror != 1) {
        R.bx = skip;
        R.ax = R.cx;
        SET_CH(0);
        bool odd = AL & 1;
        SET_AL(AL >> 1);
        do {
            SET_CL(AL);
            rep_movsw();
            if (odd) movsb();
            R.si = (uint16_t)(R.si + R.bx);
            R.di = add16(R.di, R.dx);
            SET_AH(AH - 1);
        } while (AH);
    } else {
        R.si = (uint16_t)(R.si + cols);
        R.bx = (uint16_t)(skip + cols + cols);
        SET_AH(cols);
        do {
            do {
                R.si--;
                uint8_t v = mb(R.ds, R.si);
                SET_AL(v);
                stosb(v);
                SET_CL(CL - 1);
            } while (CL);
            SET_CL(AH);
            R.si = (uint16_t)(R.si + R.bx);
            R.di = add16(R.di, R.dx);
            SET_CH(CH - 1);
        } while (CH);
    }
    R.zf = true, R.sf = false;
    R.bx = obj, R.ds = ds, R.es = es;
}

/* Gfx_RestoreUnder 1000:05b4
   BX = object: if it was drawn, the background saved at [bx+6]:0000 goes back over it and it is marked
   not drawn. Registers kept (DI = [bx+15] as read); CF from the last row's step. */
void Gfx_RestoreUnder(void)
{
    R.di = mw(R.ds, (uint16_t)(R.bx + 0x15));
    if (R.di == 0xffff) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    Cpu saved = R;
    mww(R.ds, (uint16_t)(R.bx + 0x15), 0xffff);
    uint8_t cols = mb(R.ds, (uint16_t)(R.bx + 0x1a));
    uint8_t rows = mb(R.ds, (uint16_t)(R.bx + 0x19));
    R.cx = cols;
    R.ds = mw(R.ds, (uint16_t)(R.bx + 6));
    R.si = 0;
    R.es = SEG_BACK;
    uint16_t step = (uint16_t)(0x110 - cols);
    do {
        bool odd = R.cx & 1;
        R.cx >>= 1;
        rep_movsw();
        if (odd) movsb();
        R.cx = cols;
        R.di = add16(R.di, step);
        rows--;
    } while (rows);
    bool cf = R.cf;
    R = saved;
    R.cf = cf, R.zf = true, R.sf = false;
}

/* Gfx_SaveUnder 1000:05f3
   BX = object: if it is on screen and has a save segment, its back-buffer rectangle is copied to
   [bx+6]:0000 (rows of [bx+1a] bytes, packed). DS, ES, BX, BP kept; AX the buffer's skip, CX the
   columns, DX = columns << 8, SI, DI past the copy. */
void Gfx_SaveUnder(void)
{
    R.si = mw(R.ds, (uint16_t)(R.bx + 0x15));
    if (R.si == 0xffff) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    R.ax = mw(R.ds, (uint16_t)(R.bx + 6));
    if (R.ax == 0) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    uint16_t ds = R.ds, es = R.es;
    uint8_t cols = mb(ds, (uint16_t)(R.bx + 0x1a));
    uint8_t rows = mb(ds, (uint16_t)(R.bx + 0x19));
    R.cx = cols;
    R.es = R.ax;
    R.di = 0;
    R.ds = SEG_BACK;
    R.ax = (uint16_t)(0x110 - cols);
    do {
        bool odd = R.cx & 1;
        R.cx >>= 1;
        rep_movsw();
        if (odd) movsb();
        R.cx = cols;
        R.si = add16(R.si, R.ax);
        rows--;
    } while (rows);
    R.dx = (uint16_t)(cols << 8);
    R.zf = true, R.sf = false;
    R.ds = ds, R.es = es;
}

/* Gfx_ClipObject 1000:0630
   BX = object: clips its current frame to the window, 256 wide (x 0..ffh) and 200 high (y 0..c7h).
   Sets the source offset [bx+0d] (frame * frame size, advanced past what is cut on the left and the
   top), the columns [bx+1a] and the source skip [bx+17], the rows [bx+19], the back-buffer offset
   [bx+15] (y * 110h + x + 888h). Off screen: [bx+15] = FFFFh, CF set. Returns AX = the rows, DX the
   skip, SI, DI the offsets; CX the y, or a part sum where it stopped.
   As the original: clipped at the top, the source moves on by the visible rows times [bx+0f], not
   the hidden rows times the row length. */
void Gfx_ClipObject(void)
{
    uint16_t o = R.bx;
    R.di = mw(R.ds, (uint16_t)(o + 2));
    uint32_t p = (uint32_t)mw(R.ds, (uint16_t)(o + 0x13)) * mw(R.ds, (uint16_t)(o + 0x0b));
    R.ax = (uint16_t)p;
    R.si = R.ax;
    R.dx = 0;
    R.cx = mw(R.ds, (uint16_t)(o + 0x11));
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
    mbw(R.ds, (uint16_t)(o + 0x1a), CL);
    mww(R.ds, (uint16_t)(o + 0x17), R.dx);
    R.ax = mw(R.ds, (uint16_t)(o + 0x0f));
    R.cx = mw(R.ds, (uint16_t)(o + 4));
    if (R.cx & 0x8000) {
        int32_t s = (int16_t)R.ax + (int16_t)R.cx;
        R.ax = (uint16_t)s;
        if (s <= 0) goto off;
        R.si = (uint16_t)(R.si + (uint16_t)((uint32_t)R.ax * mw(R.ds, (uint16_t)(o + 0x0f))));
    } else {
        if (R.cx >= 0xc8) goto off;
        R.cx = (uint16_t)(R.cx + R.ax);
        if (R.cx > 0xc8) {
            R.cx = (uint16_t)(R.cx - R.ax);
            R.ax = (uint16_t)(0xc8 - R.cx);
        } else {
            R.cx = (uint16_t)(R.cx - R.ax);
        }
        R.di = (uint16_t)(R.di + (uint16_t)(R.cx * 0x110u));
    }
    mww(R.ds, (uint16_t)(o + 0x0d), R.si);
    R.di = (uint16_t)(R.di + 0x888);
    mww(R.ds, (uint16_t)(o + 0x15), R.di);
    mbw(R.ds, (uint16_t)(o + 0x19), AL);
    R.cf = false;
    return;
off:
    mww(R.ds, (uint16_t)(o + 0x15), 0xffff);
    R.cf = true;
}

/* Gfx_FrameObject 1000:06cc
   BX = object, AL = colour: a one-pixel rectangle around its clipped area, one pixel outside it. The
   clip's carry is not tested (as the original: an object off screen gets a frame from stale values
   at FFFFh - 111h). Registers kept; CF from the last side row's step. */
void Gfx_FrameObject(void)
{
    Cpu saved = R;
    uint8_t colour = AL;
    Gfx_ClipObject();
    R.es = SEG_BACK;
    R.di = (uint16_t)(mw(R.ds, (uint16_t)(R.bx + 0x15)) - 0x111);
    uint16_t rows = mb(R.ds, (uint16_t)(R.bx + 0x19));
    uint16_t cols = mb(R.ds, (uint16_t)(R.bx + 0x1a));
    for (uint16_t n = cols; n; n--) stosb(colour);
    stosb(colour);
    stosb(colour);
    uint16_t step = (uint16_t)(0x110 - cols - 2);
    R.di = (uint16_t)(R.di + step);
    uint16_t n = rows;
    do {   /* `loop`: no rows (a stale clip) is 65,536 */
        stosb(colour);
        R.di = (uint16_t)(R.di + cols);
        stosb(colour);
        R.di = add16(R.di, step);
        n--;
    } while (n);
    for (uint16_t k = cols; k; k--) stosb(colour);
    stosb(colour);
    stosb(colour);
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    R = saved;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* ---- rectangles, the tile map, the presents of the front end ---- */

/* Gfx_DrawTileMap 1000:0710
   DI = back-buffer position (from the window's corner), SI = the map's image record, BX = the tile
   set's record (both in DS). The map's segment holds the columns, the rows, then one tile number per
   byte, row by row; a tile is 8 x 8 bytes, 40h apart in the set's segment. Drawn opaquely. Registers
   kept; CF from the last row's step. */
void Gfx_DrawTileMap(void)
{
    Cpu saved = R;
    R.es = SEG_BACK;
    R.di = (uint16_t)(R.di + 0x888);
    uint16_t tiles = mw(R.ds, (uint16_t)(R.bx + 0x12));
    uint16_t map = mw(R.ds, (uint16_t)(R.si + 0x12));
    uint8_t cols = mb(map, 0), rows = mb(map, 1);
    uint16_t src = 2;
    do {
        uint16_t row = R.di;
        for (uint8_t c = cols; c; c--) {
            R.ds = tiles;
            R.si = (uint16_t)(mb(map, src++) << 6);
            for (int y = 0; y < 8; y++) {
                for (int k = 0; k < 4; k++) movsw();
                if (y < 7) R.di = (uint16_t)(R.di + 0x108);
            }
            R.di = (uint16_t)(R.di - 0x770);   /* back to the tile's top row, 8 on */
        }
        R.di = add16(row, 0x880);
        rows--;
    } while (rows);
    bool cf = R.cf;
    R = saved;
    R.cf = cf, R.zf = true, R.sf = false;
}

/* Gfx_RecolourRect 1000:07e5
   The back-buffer rectangle at x AX, y BX, SI wide, CX high: every pixel of colour DH becomes DL.
   Registers kept; CF from the last row's step. */
void Gfx_RecolourRect(void)
{
    Cpu saved = R;
    R.es = R.ds = SEG_BACK;
    uint16_t di = (uint16_t)(row_110(R.bx) + 0x888 + R.ax);
    uint16_t w = R.si, h = R.cx;
    do {   /* `loop`: a height or width of 0 is 65,536 */
        R.di = R.si = di;
        uint16_t n = w;
        do {
            uint8_t v = lodsb();
            if (v == DH) stosb(DL);
            else R.di++;
            n--;
        } while (n);
        di = add16(di, 0x110);
        h--;
    } while (h);
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    R = saved;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Gfx_FillRect 1000:0823
   The back-buffer rectangle at x AX, y BX, DX wide, CX high in the colour SI's low byte (words, then
   the odd byte). Registers kept; CF from the last row's step. */
void Gfx_FillRect(void)
{
    Cpu saved = R;
    R.es = SEG_BACK;
    R.di = (uint16_t)(row_110(R.bx) + 0x888 + R.ax);
    uint8_t colour = lo8(R.si);
    uint16_t step = (uint16_t)(0x110 - R.dx);
    uint16_t rows = R.cx;
    bool odd = R.dx & 1;
    uint16_t words = R.dx >> 1;
    do {   /* a height of 0 is 65,536 rows, as the original */
        R.cx = words;
        rep_stosw((uint16_t)(colour << 8 | colour));
        if (odd) stosb(colour);
        R.di = add16(R.di, step);
        rows--;
    } while (rows);
    bool cf = R.cf;
    R = saved;
    R.cf = cf, R.zf = true, R.sf = false;
}

/* The shared loop of Gfx_FillRows and Gfx_ClearBuffer (1000:087c): BL rows of 256 pixels from DI.
   The original fills with rep stosd from EAX = AL four times; that high half of EAX is left so after
   the return but nothing reads EAX later (the only other 32-bit use, 1000:31b6, zeroes it). */
static void fill_rows(uint16_t di, uint8_t rows, uint8_t colour)
{
    Cpu saved = R;
    R.es = SEG_BACK;
    R.di = di;
    uint16_t w = (uint16_t)(colour << 8 | colour);
    do {
        for (int k = 0; k < 0x80; k++) stosw(w);
        R.di = add16(R.di, 0x10);
        rows--;
    } while (rows);
    bool cf = R.cf;
    R = saved;
    R.cf = cf, R.zf = true, R.sf = false;
}

/* Gfx_FillRows 1000:0862
   CL whole rows (0: 256) of the back buffer from row BX in colour AL. Registers kept. */
void Gfx_FillRows(void)
{
    fill_rows((uint16_t)(row_110(R.bx) + 0x888), CL, AL);
}

/* Gfx_ClearBuffer 1000:0876
   The 200 rows of the window in colour AL. Registers kept. */
void Gfx_ClearBuffer(void)
{
    fill_rows(0x888, 0xc8, AL);
}

/* The shared loop of the presents (1000:08c5): BL rows of 256 bytes from 6d78:SI (stride 110h) to
   A000:DI (stride 140h), with rep movsd in the original (not patched: these always move doublewords). */
static void present_rows(uint16_t si, uint16_t di, uint8_t rows)
{
    Cpu saved = R;
    R.es = SEG_VGA;
    R.ds = SEG_BACK;
    R.si = si, R.di = di;
    do {
        for (int k = 0; k < 0x40; k++) movsd();
        R.di = (uint16_t)(R.di + 0x40);
        R.si = add16(R.si, 0x10);
        rows--;
    } while (rows);
    bool cf = R.cf;
    R = saved;
    R.cf = cf, R.zf = true, R.sf = false;
}

/* Gfx_PresentRows 1000:089c
   CL rows (0: 256) from row AX of the window to the screen (row AX, x 32). Registers kept. */
void Gfx_PresentRows(void)
{
    uint16_t row = (uint16_t)(R.ax << 8 & 0xff00);   /* AL << 8, as the original builds it */
    uint16_t si = (uint16_t)(row + (uint16_t)(R.ax << 4) + 0x888);
    uint16_t di = (uint16_t)(row + (uint16_t)(R.ax << 6) + 0x20);
    present_rows(si, di, CL);
}

/* Gfx_Present 1000:08bc
   The whole 256 x 200 window to the screen at x 32. Registers kept. */
void Gfx_Present(void)
{
    present_rows(0x888, 0x20, 0xc8);
}

/* Gfx_LoadBits 1000:482f
   The race's small graphics: ..\BITSFILE.PH0 (the name at 193c:1231) through Unpack_LoadSeries into
   1b78, then its first 6e00h bytes copied to 193c:3fe3..ade3 (the skid and burst frames, the HUD's
   digits and icons, the banners: docs/re/drawing.md). Registers kept, CF clear. */
void Gfx_LoadBits(void)
{
    Cpu saved = R;
    R.ds = SEG_DATA;
    R.si = 0x1231;
    R.dx = SEG_TILES;
    Unpack_LoadSeries();
    R.ds = SEG_TILES;
    R.es = SEG_DATA;
    R.si = 0;
    R.di = 0x3fe3;
    R.cx = (uint16_t)(0xade3 - R.di);
    rep_movsb();
    R = saved;
    R.cf = false, R.zf = false, R.sf = false;   /* the sub that sized the copy, then clc */
}
