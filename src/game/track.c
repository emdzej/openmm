/* The track: its files and the tile animations (docs/re/functions.md, "track"; docs/re/race-setup.md;
   the formats in docs/formats/). A theme (1..9) is a set of files in the data-set directory GAME1: the
   tile graphics ROUNDnBR.PR0.. (16 x 16 pictures, packed), the tile compositions ROUNDnBR.CT, the
   collision bitmaps ROUNDn.COL, the surface cells ROUNDn.DIR, the palette, the vehicles; a track of the
   theme adds its map ROUNDnm.MAP and its ROUNDnmB.BRK. The race keeps the expanded map as words of
   graphic-tile numbers in two blocks, 3b78:0000 (rows 0-95) and 4478:0000 (rows 96-191), 384 bytes a
   row; the animations below rewrite entries of those blocks, or the first graphic tile itself. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* A segment operand of the program: relocated by the load segment like every segment constant of the
   image (MZ relocations), so the buffer the code calls 1b78 is at 2b78 in memory with the image at
   1000h. The notes use the operands' numbering (docs/re/race-setup.md). */
#define LSEG(raw) ((uint16_t)(SEG_CODE + (raw)))

/* Opens the file named at DS:DX for reading (int 21h 3Dh, AL = C0h: no inheritance, deny none, read)
   and leaves BX = AX = the handle or the error code, CF as DOS returns it. */
static void open_into_bx(uint16_t name)
{
    R.dx = name;
    R.ax = 0x3dc0;
    mach_int(0x21);
    R.bx = R.ax;
}

/* Reads CX bytes into SEG_DATA:DX from the handle in BX, then closes it (int 21h 3Fh, 3Eh). */
static void read_close(uint16_t dst, uint16_t count)
{
    R.dx = dst;
    R.cx = count;
    R.ax = SEG_DATA;
    R.ds = SEG_DATA;
    SET_AH(0x3f);
    mach_int(0x21);
    SET_AH(0x3e);
    mach_int(0x21);
}

/* Track_SetNames 1000:3b50
   Into the data-set directory ([042a], "GAME1"), then the theme's digit ([28bf] + '0') into the seven
   per-theme names (bytes 1205, 1210, 11eb, 11f8, 11b1, 11bc, 1229) and the theme and track digits
   ([28c0] + '0') as a word into the map's and the .BRK's names (11a5, 121c). The track's .BRK (name at
   1217) is read whole (up to 200h bytes) to [195b], the theme's .LEV (1224, up to 80h) to [1b5b]; [28bb]
   and [28b9] point at them, or are 0 when the file does not open. [28bd] = the theme's terrain table
   (word [26d7 + 2 (theme - 1)]). Theme 9 clears the presence words of vehicles 1-3 ([13b2] [1516]
   [167a]) and sets [27b5].
   Out: AX = the terrain table, BX = 2 (theme - 1), DS = 193c; CX and DX as the file reads left them;
   flags from comparing the theme with 9 (CF = theme < 9). */
void Track_SetNames(void)
{
    R.ax = SEG_DATA;
    R.ds = SEG_DATA;
    R.dx = 0x042a;
    SET_AH(0x3b);
    mach_int(0x21);
    uint8_t theme = (uint8_t)(d8(0x28bf) + '0');
    uint8_t track = (uint8_t)(d8(0x28c0) + '0');
    R.ax = (uint16_t)(track << 8 | theme);
    d8w(0x1205, theme);
    d8w(0x1210, theme);
    d8w(0x11eb, theme);
    d8w(0x11f8, theme);
    d8w(0x11b1, theme);
    d8w(0x11bc, theme);
    d8w(0x1229, theme);
    d16w(0x11a5, R.ax);
    d16w(0x121c, R.ax);

    open_into_bx(0x1217);   /* ROUNDnmB.BRK */
    if (R.cf) {
        R.ax = 0;
    } else {
        read_close(0x195b, 0x200);
        R.ax = 0x195b;
    }
    d16w(0x28bb, R.ax);
    open_into_bx(0x1224);   /* ROUNDnBR.LEV */
    if (R.cf) {
        R.ax = 0;
    } else {
        read_close(0x1b5b, 0x80);
        R.ax = 0x1b5b;
    }
    d16w(0x28b9, R.ax);

    R.ax = SEG_DATA;
    R.ds = SEG_DATA;
    uint8_t t = d8(0x28bf);
    R.bx = (uint8_t)((uint8_t)(t - 1) << 1);
    R.ax = d16((uint16_t)(R.bx + 0x26d7));
    d16w(0x28bd, R.ax);
    R.cf = t < 9;
    flags_zs8((uint8_t)(t - 9));
    if (t == 9) {
        d16w(0x13b2, 0);
        d16w(0x1516, 0);
        d16w(0x167a, 0);
        d16w(0x27b5, 1);
    }
}

/* Track_LoadColDir 1000:458b
   The theme's collision bitmaps ROUNDn.COL (name at 193c:11ac) whole into 193c:3163 and its surface
   cells ROUNDn.DIR (11b7) into 193c:35e3. CF set if either fails to open; then, as the original, the
   close that follows uses the error code in BX as a handle. Registers kept (ES is not touched). */
void Track_LoadColDir(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = SEG_DATA;
    bool fail = true;
    open_into_bx(0x11ac);
    if (!R.cf) {
        read_close(0x3163, 0xffff);
        open_into_bx(0x11b7);
        if (!R.cf) {
            read_close(0x35e3, 0xffff);
            fail = false;
        }
    }
    if (fail) {
        SET_AH(0x3e);
        mach_int(0x21);
    }
    bool zf = R.zf, sf = R.sf;
    R = saved;
    R.cf = fail;
    R.zf = zf, R.sf = sf;
}

/* Track_LoadTiles 1000:45e5
   The theme's graphic tiles (1000:45ef), then the head of the result into 193c:3ee3 (1000:4808). */
void Track_LoadTiles(void)
{
    Track_LoadTileGfx();
    if (R.cf) return;
    Track_CopyTileTable();
    R.cf = false;
}

/* Track_LoadTileGfx 1000:45ef
   ROUNDnBR.PR0, PR1, .. (the series' name at 193c:11e6) through the packed-file loader into 1b78:0000:
   the theme's 16 x 16 graphic tiles, 256 bytes each. Registers and flags kept (pushf/popf), CF clear. */
void Track_LoadTileGfx(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = SEG_DATA;
    R.si = 0x11e6;
    R.dx = LSEG(0x1b78);
    Unpack_LoadSeries();
    R = saved;
    R.cf = false;
}

/* Track_CopyTileTable 1000:4808
   The first graphic tile (100h bytes at 1b78:0000) into 193c:3ee3: the backdrop pattern that
   Track_ScrollBackdrop scrolls back into tile 0 on themes 1, 3 and 5. Registers and flags kept. */
void Track_CopyTileTable(void)
{
    for (uint16_t i = 0; i < 0x80; i++) d16w((uint16_t)(0x3ee3 + 2 * i), mw(LSEG(0x1b78), (uint16_t)(2 * i)));
}

/* Track_ScrollBackdrop 1000:8996
   Themes 1, 3 and 5, from the race picture on each presented frame: graphic tile 0 (16 rows of 16 bytes,
   one paragraph each from segment 1b78) is rewritten from the pattern kept at 193c:3ee3, shifted right by
   ([264a] mod 32) / 2 columns and down by ([264c] mod 32) / 2 rows with wrap-around, so the backdrop
   moves at half the speed of the camera. Rows go to successive segments 1b78 + row (wrapping from 1b88
   back to 1b78), columns wrap within the row (di & 0Fh).
   Out: AX = 1b78 + the row shift, BX = 0, CX = the segment after the last row, DX = the row shift, DI =
   the column shift; SI, DS, ES kept; CF from the last row's wrap test, ZF set (dec bh). */
void Track_ScrollBackdrop(void)
{
    uint16_t dx = (uint16_t)((d16(0x264c) & 0x1f) >> 1);
    uint16_t di = (uint16_t)((d16(0x264a) & 0x1f) >> 1);
    uint16_t si = 0x3ee3;
    uint16_t cx = (uint16_t)(LSEG(0x1b78) + dx);
    R.ax = cx;
    bool cf = false;
    for (int row = 0; row < 16; row++) {
        uint16_t es = cx;
        for (int col = 0; col < 16; col++) {
            mbw(es, di, d8(si));
            si++;
            di = (uint16_t)((di + 1) & 0x0f);
        }
        cx++;
        cf = cx < LSEG(0x1b88);                      /* cmp cx, 1b88h: jl keeps it */
        if ((int16_t)cx >= LSEG(0x1b88)) cx = (uint16_t)(cx - 0x10), cf = false;
    }
    R.bx = 0;
    R.cx = cx;
    R.dx = dx;
    R.di = di;
    R.cf = cf;
    R.zf = true, R.sf = false;
}

/* Track_AnimateWater2 1000:89e0
   Theme 2, each presented frame: the counter [26d1] (in the caller's DS) steps; bits 2-3 of it pick one
   of four sets of 16 consecutive graphic tiles (base 00h, 10h, 20h, 30h), written as a 4 x 4 block of map
   entries at 4478:8046 (rows 180h bytes apart): the water changes every four frames. Registers kept;
   flags from the last row's step of DI. */
void Track_AnimateWater2(void)
{
    uint16_t n = (uint16_t)(mw(R.ds, 0x26d1) + 1);
    mww(R.ds, 0x26d1, n);
    uint16_t ax = (uint16_t)(((n >> 2) & 3) * 0x10);
    uint16_t di = 0x8046;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            mww(LSEG(0x4478), di, ax);
            di = (uint16_t)(di + 2);
            ax++;
        }
        uint32_t sum = (uint32_t)di + 0x178;
        R.cf = sum > 0xffff;
        di = (uint16_t)sum;
    }
    flags_zs16(di);
}

/* One 3 x 3 block of map entries from seg:off (rows 180h bytes apart), values v .. v + 8. */
static uint16_t block3(uint16_t seg, uint16_t off, uint16_t v)
{
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            mww(seg, (uint16_t)(off + row * 0x180 + col * 2), v);
            if (row != 2 || col != 2) v++;
        }
    return v;
}

/* Track_AnimateTiles8 1000:8a2b
   Theme 8, each presented frame, except on track 1: the counter [26d3] steps; bits 2-3 pick the frame
   (0: tiles from 0, 1: from 9, 2: from 12h; 3 resets the counter and shows frame 0), written as 3 x 3
   blocks of map entries: on track 3 six blocks (3b78:5db4, 66b4, 78b4, 81b4 and 4478:0cb4, 15b4), on
   the others one (3b78:6624). AX, BX, DS kept; flags from the last step (inc ax; CF from the track
   compares or the sub of the block before). */
void Track_AnimateTiles8(void)
{
    uint8_t track = d8(0x28c0);
    if (track == 1) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    uint16_t n = (uint16_t)(d16(0x26d3) + 1);
    d16w(0x26d3, n);
    uint16_t ax;
    switch ((n >> 2) & 3) {
    case 1: ax = 9; break;
    case 2: ax = 0x12; break;
    case 3: d16w(0x26d3, 0); ax = 0; break;
    default: ax = 0; break;
    }
    uint16_t last;
    if (track != 3) {
        last = block3(LSEG(0x3b78), 0x6624, ax);
        R.cf = track < 3;
    } else {
        static const uint16_t offs[6] = {0x5db4, 0x66b4, 0x78b4, 0x81b4, 0x0cb4, 0x15b4};
        for (int b = 0; b < 6; b++) last = block3(b < 4 ? LSEG(0x3b78) : LSEG(0x4478), offs[b], ax);
        R.cf = false;   /* sub ax, 8 before the last block: no borrow */
    }
    flags_zs16(last);
}
