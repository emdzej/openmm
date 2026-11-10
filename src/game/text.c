/* Text (docs/re/drawing.md, "Fonts"). The front end's fonts are pictures of 8-pixel-wide cells, one per
   character of 0-9, A-Z, ! and ? (38 glyphs, in that order), each the font's height in rows, drawn
   into the back buffer with colour 0 transparent. Strings are zero-terminated in DS; a space is a
   blank cell. */
#include "cpu.h"
#include "gfx.h"
#include "mem.h"
#include "routines.h"

/* Text_DrawNth 1000:08f0
   Skips CX zero-terminated strings from SI and draws the next: centred if AX = -1, else at x AX;
   BX = row, DX = font record. Registers kept (flags from the draw). */
void Text_DrawNth(void)
{
    Cpu saved = R;
    if (R.cx) {
        do {
            while (lodsb() != 0) {}
        } while (--R.cx);
    }
    if (R.ax == 0xffff) Text_DrawCentred();
    else Text_Draw();
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    R = saved;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Text_DrawCentred 1000:0910
   The string at SI centred in the 256-pixel window: x = 127 - 4 * length (so a string of odd length
   is half a cell right of centre). BX = row, DX = font record. Registers kept. */
void Text_DrawCentred(void)
{
    uint16_t ax = R.ax, si = R.si, dx = R.dx;
    uint16_t x = 0x7f;
    while (lodsb() != 0) x = (uint16_t)(x - 4);
    R.ax = x;
    R.si = si;
    Text_Draw();
    R.ax = ax, R.dx = dx;
}

/* Text_Draw 1000:0929
   The string at SI at x AX, row BX, in the font of the record at DX (+0d the height, +12 the glyphs'
   segment). A negative x skips the whole cells left of the window and starts the rest up to 7 pixels
   into the left margin. Cells are 8 pixels apart; it stops at the end of the string or when a cell
   would start at x 0ffh or beyond. Registers kept; CF clear unless the last step was a cell's advance. */
void Text_Draw(void)
{
    Cpu saved = R;
    uint16_t font = R.dx;
    uint16_t di = (uint16_t)(row_110(R.bx) + 0x888);
    uint16_t start = R.si;
    while (lodsb() != 0) {}
    uint16_t cx = (uint16_t)(R.si - start);   /* the length and its terminator */
    R.si = start;
    uint16_t x = R.ax;
    if (x & 0x8000) {
        uint16_t left = (uint16_t)-x;
        uint16_t cells = left >> 3;
        R.si = (uint16_t)(R.si + cells);
        cx = (uint16_t)(cx - cells);
        di = (uint16_t)(di - (left & 7));
        x = (uint16_t)-(left & 7);
    } else {
        di = (uint16_t)(di + x);
    }
    R.es = SEG_BACK;
    R.dx = mw(R.ds, (uint16_t)(font + 0x12));
    SET_AH(mb(R.ds, (uint16_t)(font + 0x0d)));
    R.bx = x;
    R.cf = false;
    R.di = di;
    if ((int16_t)cx > 0) {
        do {
            R.bx = (uint16_t)(R.bx + 8);
            if (R.bx >= 0x107) {
                R.cf = false;
                break;
            }
            uint8_t c = lodsb();
            SET_AL(c);
            if (c == 0) {
                R.cf = false;
                break;
            }
            if (c != ' ') {
                uint16_t ds = R.ds;
                Text_DrawGlyph();
                R.ds = ds;
            }
            R.di = add16(R.di, 8);
        } while (--cx);
    }
    bool cf = R.cf;
    R = saved;
    R.cf = cf;
}

/* Text_DrawGlyph 1000:0999
   AL = the character, AH = the rows, DX = the glyphs' segment, ES:DI = where: one 8-pixel cell, colour 0
   transparent. '!' is glyph 36, '?' 37, else the character less '0', less 7 more above '9' (so A-Z
   follow 0-9); other characters give glyphs outside the 38, read from whatever follows (as the
   original: a byte below '0' is not moved by 7, the compare is signed). The cell's offset is the glyph
   times 8 * rows in 8 bits. Registers kept except DS, left at the glyphs' segment (popa does not restore
   it); CF from the last row's step. */
void Text_DrawGlyph(void)
{
    Cpu saved = R;
    R.ds = R.dx;
    uint8_t rows = AH;
    uint8_t per = (uint8_t)(rows << 3);
    uint8_t c = AL, g;
    if (c == '!') g = 0x24;
    else if (c == '?') g = 0x25;
    else {
        g = (uint8_t)(c - '0');
        if ((int8_t)g > 9) g = (uint8_t)(g - 7);
    }
    R.si = (uint16_t)(g * per);
    do {
        for (int n = 0; n < 8; n++) {
            uint8_t v = lodsb();
            if (v) stosb(v);
            else R.di++;
        }
        R.di = add16(R.di, 0x108);
    } while (--rows);
    bool cf = R.cf;
    R = saved;
    R.ds = saved.dx;
    R.cf = cf, R.zf = true, R.sf = false;
}

/* Text_TwoDigits 1000:1a34
   AL = 0..99: its two ASCII digits into [SI] and [SI+1] (DS), a leading zero as a space. Registers
   kept; ZF set if the tens were 0. */
void Text_TwoDigits(void)
{
    uint8_t v = AL;
    uint8_t tens = (uint8_t)(v / 10 + '0'), units = (uint8_t)(v % 10 + '0');
    R.zf = tens == '0';
    R.sf = false;
    R.cf = false;
    if (tens == '0') tens = ' ';
    mww(R.ds, R.si, (uint16_t)(units << 8 | tens));
}
