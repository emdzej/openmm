/* FONT.BIN, the code-card check, ported (font.h; how it works: docs/re/protection.md). The program is
   in the overlay segment (2424, SEG_OVL) and keeps its variables there: its texts, palettes, pictures
   and the card's table are read from the file the game loaded, never copied into the source. It draws
   in mode 10h through the VGA's planes, so every access to A000 goes through the planar model
   (vga_planar.h). Names: tools/ghidra/names-font.tsv; addresses FONT:offset. */
#include "font.h"
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "vga_planar.h"

/* the overlay's own variables (cs: in the original) */
static uint8_t cb(uint16_t o) { return mb(SEG_OVL, o); }
static uint16_t cw(uint16_t o) { return mw(SEG_OVL, o); }
static void cbw(uint16_t o, uint8_t v) { mbw(SEG_OVL, o, v); }
static void cww(uint16_t o, uint16_t v) { mww(SEG_OVL, o, v); }
static int step(void) { return R.df ? -1 : 1; }

/* flags as the instructions leave them, where the next test or the return depends on them */
static uint16_t add16(uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    R.cf = r > 0xffff;
    flags_zs16((uint16_t)r);
    return (uint16_t)r;
}
static uint16_t sub16(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    flags_zs16((uint16_t)(a - b));
    return (uint16_t)(a - b);
}
static uint8_t add8(uint8_t a, uint8_t b)
{
    R.cf = a + b > 0xff;
    flags_zs8((uint8_t)(a + b));
    return (uint8_t)(a + b);
}
static void cmp8(uint8_t a, uint8_t b) { R.cf = a < b, flags_zs8((uint8_t)(a - b)); }
static uint8_t logic8(uint8_t r) { R.cf = false, flags_zs8(r); return r; }       /* and, or, xor */
static uint16_t logic16(uint16_t r) { R.cf = false, flags_zs16(r); return r; }
static uint8_t dec8(uint8_t a) { flags_zs8((uint8_t)(a - 1)); return (uint8_t)(a - 1); }   /* CF kept */
static uint8_t inc8(uint8_t a) { flags_zs8((uint8_t)(a + 1)); return (uint8_t)(a + 1); }
static uint16_t dec16(uint16_t a) { flags_zs16((uint16_t)(a - 1)); return (uint16_t)(a - 1); }
static uint16_t shr16(uint16_t a, uint8_t n)
{
    n &= 0x1f;                         /* the 286 and later mask the count; 0: no flags touched */
    if (!n) return a;
    R.cf = n <= 16 && (a >> (n - 1) & 1);
    uint16_t r = n >= 16 ? 0 : (uint16_t)(a >> n);
    flags_zs16(r);
    return r;
}
static uint16_t mul8(uint8_t a, uint8_t b)   /* mul r8: AX = AL * r8, CF = OF = AH <> 0 */
{
    uint16_t r = (uint16_t)(a * b);
    R.cf = r > 0xff;
    return r;
}
/* div r8: AL = AX / r8, AH = the remainder. A quotient over 255 (or a divisor of 0) is the CPU's divide
   error, int 0, which nothing here installs: the original would crash. The inputs here cannot make one
   (the cursor stays on the grid), so the port keeps the low byte rather than modelling the crash. */
static void div8(uint8_t d)
{
    uint16_t a = R.ax;
    uint16_t q = d ? (uint16_t)(a / d) : 0xff, r = d ? (uint16_t)(a % d) : 0;
    R.ax = (uint16_t)((r & 0xff) << 8 | (q & 0xff));
}

/* the three registers the original resets before each text and at the exit: write mode 0, all planes,
   no rotation or function */
static void planes_default(void)
{
    io_out16(0x3ce, 0x0005);
    io_out16(0x3c4, 0x0f02);
    io_out16(0x3ce, 0x0003);
}

/* int 10h 1301h with the page of text at cs:bp: 960 characters (12 rows of 80) from row 0, column 0,
   XORed in colour 15 */
static void page_text(uint16_t bp)
{
    R.ax = 0x1301, R.bx = 0x00ff, R.cx = 0x03c0, R.dx = 0;
    R.es = SEG_OVL, R.bp = bp;
    mach_int(0x10);
}

/* one character at cs:11bf, XORed in colour 8, at row dh, column dl (ES is still the overlay's) */
static void cell_text(uint8_t dh, uint8_t dl)
{
    R.ax = 0x1301, R.bx = 0x00f8, R.cx = 1;
    R.dx = (uint16_t)(dh << 8 | dl);
    R.bp = 0x11bf;
    mach_int(0x10);
}

static void wait_key(void)
{
    SET_AH(0);
    mach_int(0x16);
}

/* the exits: the planes' registers back (not on the exit after two wrong answers: as the original),
   text mode 3, the nine registers pushed at the entry popped, AX the verdict */
static void leave(const Cpu *in, bool pass, bool reset)
{
    if (reset) planes_default();
    R.ax = 0x0003;
    mach_int(0x10);
    R.bp = in->bp, R.ds = in->ds, R.si = in->si, R.di = in->di, R.es = in->es;
    R.dx = in->dx, R.cx = in->cx, R.bx = in->bx;
    if (pass) R.ax = logic16(0);   /* xor ax, ax */
    else R.ax = 0xffff;            /* the flags as the mode set left them */
}

/* Card_Check FONT:0000
   The far entry: AX = the card's language table (the game passes 0). Sets up the screen (mode 10h, the
   symbol box drawn off screen at A000:8000, the backgrounds) with a black palette, then the real one, and
   asks. A wrong first answer gets "the wrong symbol" and a second chance; a right answer, from either,
   gets "one more just to check" and a further question, which must be right too. Two wrong answers, or
   a wrong one after a right one, fail. Returns AX = 0 (pass) or FFFFh (fail), every other register as at
   the entry, in text mode 3. */
void Card_Check(void)
{
    Cpu in = R;
    R.ds = R.es = SEG_OVL;
    cww(0x254a, in.ax);
    Card_SetMode();
    R.si = 0x68aa;              /* all black while drawing */
    Card_SetPalette();
    R.di = 0x8000;
    Card_FillBack();            /* the box's backdrop, off screen */
    R.di = 0;
    Card_ClearText();           /* the text area, rows 0-167 */
    R.di = 0x35c0;
    Card_FillBack();            /* rows 172-347 */
    cww(0x2566, 0x8000);        /* the symbol box (26 bytes x 162 rows, 4 planes at cs:26ca) to A000:8000 */
    cww(0x255e, 0), cww(0x255c, 0);
    cww(0x2560, 0x26ca), cww(0x2564, 0xa2), cww(0x2562, 0x1a);
    Card_Sprite();
    /* as the original: two more pictures set up (x 78h and 1b3h, row dch, 10 bytes x 66 rows from
       cs:26ca) and never drawn; the variables keep the last values */
    cww(0x2566, 0), cww(0x255e, 0x78), cww(0x255c, 0xdc), cww(0x2564, 0x42), cww(0x2562, 0x0a);
    cww(0x2566, 0), cww(0x255e, 0x1b3), cww(0x255c, 0xdc), cww(0x2564, 0x42), cww(0x2562, 0x0a);
    R.si = 0x689a;              /* the palette shown */
    Card_SetPalette();
    Card_Ask();
    cmp8(AL, DL);
    if (!R.zf) {
        planes_default();
        R.di = 0;
        Card_ClearText();
        page_text(0x0a3f);      /* "the wrong symbol! Press ENTER." */
        wait_key();
        Card_Ask();
        cmp8(AL, DL);
        if (!R.zf) { leave(&in, false, false); return; }
    }
    planes_default();
    R.di = 0;
    Card_ClearText();
    page_text(0x0dff);          /* "Correct. Now one more just to check..." */
    wait_key();
    Card_Ask();
    cmp8(AL, DL);
    leave(&in, R.zf, true);
}

/* Card_Ask FONT:0190
   One question. The challenge comes from the BIOS's tick count (0040:006C, low byte): column = bits 0-3,
   row = bits 3-6 (bit 3 is in both: as the original); kept at cs:11c0 and cs:11c1. The question's text
   with the column letter and row number filled in, then the symbol box and the cursor (cs:256a, 4 bytes
   x 22 rows) at cs:2558 / cs:255a (x, y in pixels; kept across questions), moved by the arrows 20 pixels
   across and 18 down over an 8 x 8 grid from (e2h, b5h), wrapping into the next or previous row
   (column), until Enter. Out: AL = the card's symbol at the challenge's cell (table [cs:68ba + 2 *
   cs:254a], 16 cells a row), DL = the symbol chosen (row * 8 + column of the grid). */
void Card_Ask(void)
{
    R.ax = 0x0040;
    R.es = R.ax;
    SET_AL(mb(0x40, 0x6c));
    SET_BL(AL);
    SET_AL(logic8(AL & 0x0f));
    SET_BL(BL >> 1), SET_BL(BL >> 1), SET_BL(BL >> 1);
    SET_BL(logic8(BL & 0x0f));
    cbw(0x11c0, AL);
    cbw(0x11c1, BL);
    R.di = 0;
    Card_ClearText();
    Card_ShowQuestion();
    for (;;) {
        Card_CopyBox();
        cww(0x2566, 0);
        R.ax = cw(0x2558);
        cww(0x255e, R.ax);
        R.ax = cw(0x255a);
        cww(0x255c, R.ax);
        cww(0x2560, 0x256a), cww(0x2564, 0x16), cww(0x2562, 4);
        Card_Sprite();
        wait_key();
        cmp8(AL, 0x0d);
        if (R.zf) break;
        /* the cursor's moves: signed compares on the words, as the original's jl / jge */
        uint8_t key = AH;
        int16_t x = (int16_t)cw(0x2558), y = (int16_t)cw(0x255a);
        if (key == 0x4d) {             /* right: past the last column to the next row's first */
            x = (int16_t)(x + 0x14);
            if (x >= 0x182) {
                x = 0xe2, y = (int16_t)(y + 0x12);
                if (y >= 0x145) y = 0xb5;
            }
        } else if (key == 0x4b) {      /* left: before the first column to the previous row's last */
            x = (int16_t)(x - 0x14);
            if (x < 0xe2) {
                x = 0x16e, y = (int16_t)(y - 0x12);
                if (y < 0xb5) y = 0x133;
            }
        } else if (key == 0x48) {      /* up: above the top row to the previous column's bottom */
            y = (int16_t)(y - 0x12);
            if (y < 0xb5) {
                y = 0x133, x = (int16_t)(x - 0x14);
                if (x < 0xe2) x = 0x16e;
            }
        } else if (key == 0x50) {      /* down: below the bottom to the next column's top */
            y = (int16_t)(y + 0x12);
            if (y >= 0x145) {
                y = 0xb5, x = (int16_t)(x + 0x14);
                if (x >= 0x182) x = 0xe2, y = 0xb5;
            }
        }
        cww(0x2558, (uint16_t)x), cww(0x255a, (uint16_t)y);   /* (the compares' flags are not used) */
    }
    /* the challenge's cell, row * 16 + column */
    SET_AL(0x10);
    SET_AH(cb(0x11c1));
    R.ax = mul8(AL, AH);
    R.bx = logic16(0);
    SET_BL(cb(0x11c0));
    R.bx = add16(R.bx, R.ax);
    /* the cell chosen on the grid */
    R.ax = sub16(cw(0x255a), 0xb5);
    SET_DL(0x12);
    div8(DL);
    SET_DL(8);
    R.ax = mul8(AL, DL);
    R.dx = R.ax;
    R.ax = sub16(cw(0x2558), 0xe2);
    SET_CL(0x14);
    div8(CL);
    SET_CL(AL);
    SET_CH(0);
    R.dx = add16(R.dx, R.cx);
    /* the card's symbol there */
    R.ax = R.bx;
    R.bx = cw(0x254a);
    R.bx = (uint16_t)(R.bx << 1);
    R.cf = (cw(0x254a) & 0x8000) != 0, flags_zs16(R.bx);
    R.bx = cw((uint16_t)(R.bx + 0x68ba));
    R.bx = add16(R.bx, R.ax);
    SET_AL(cb(R.bx));
}

/* Card_SetMode FONT:033c
   Mode 10h (640 x 350, 16 colours), then the sixteen attribute registers set to 0-15 through the BIOS
   (int 10h 1000h). */
void Card_SetMode(void)
{
    R.ax = 0x0010;
    mach_int(0x10);
    SET_BL(logic8(0));
    do {
        R.ax = 0x1000;
        SET_BH(BL);
        mach_int(0x10);
        SET_BL(inc8(BL));
        SET_BL(logic8(BL & 0x0f));
    } while (!R.zf);
}

/* Card_SetPalette FONT:0352
   The sixteen attribute registers from cs:si directly at the ports (the status register from the BIOS's
   CRTC base at 0040:0063, + 6). */
void Card_SetPalette(void)
{
    R.ax = 0x0040;
    R.es = R.ax;
    R.dx = mw(0x40, 0x63);
    SET_DL(add8(DL, 6));
    R.bx = 0;
    do {
        Card_SetAttr();
        SET_BH(inc8(BH));
        cmp8(BH, 0x10);
    } while (R.cf);
}

/* Card_SetAttr FONT:036c
   Attribute register BH = the byte at ds:si (si advanced; BL keeps it): the status register read to
   reset the flip-flop, index and value to 3C0h, then 20h (the palette back to the display). As the
   original, the second read is of 3C0h (DL already C0h), not of the status register. */
void Card_SetAttr(void)
{
    R.cx = logic16(0);
    SET_AL(mb(R.ds, R.si));
    R.si = (uint16_t)(R.si + step());
    SET_BL(AL);
    uint16_t dx = R.dx;
    SET_AL(io_in8(R.dx));
    SET_DL(0xc0);
    SET_AL(BH);
    io_out8(R.dx, AL);
    SET_AL(BL);
    io_out8(R.dx, AL);
    SET_AL(io_in8(R.dx));
    SET_DL(0xc0);
    SET_AL(0x20);
    io_out8(R.dx, AL);
    R.dx = dx;
}

/* Card_Fill FONT:0385
   CX words of colour BL (write mode 2: each plane its colour bit) from A000:di ([cs:2556]), then write
   mode 0. */
void Card_Fill(void)
{
    R.ax = cw(0x2556);
    R.es = R.ax;
    R.dx = 0x3c4, R.ax = 0x0f02;
    io_out16(R.dx, R.ax);
    R.dx = 0x3ce, R.ax = 0x0003;
    io_out16(R.dx, R.ax);
    R.dx = 0x3ce, R.ax = 0x0205;
    io_out16(R.dx, R.ax);
    R.ax = R.bx;
    for (; R.cx; R.cx--) {
        vmem_wr8(R.es, R.di, AL);
        vmem_wr8(R.es, (uint16_t)(R.di + 1), AH);
        R.di = (uint16_t)(R.di + 2 * step());
    }
    R.dx = 0x3ce, R.ax = 0x0005;
    io_out16(R.dx, R.ax);
}

/* Card_SetStart FONT:03ac
   The CRTC's start address = BX. Not called (dead code). */
void Card_SetStart(void)
{
    R.dx = 0x3d4;
    SET_AL(0x0c);
    SET_AH(BH);
    io_out16(R.dx, R.ax);
    SET_AL(inc8(AL));
    SET_AH(BL);
    io_out16(R.dx, R.ax);
}

/* Card_ClearText FONT:03bc
   Rows 0-167 from DI (0) in colour 9: the text area. */
void Card_ClearText(void)
{
    R.cx = 0x1a40, R.bx = 0x0909;
    Card_Fill();
}

/* Card_FillBack FONT:03c6
   1b80h words from DI in colour 10 (176 rows). */
void Card_FillBack(void)
{
    R.cx = 0x1b80, R.bx = 0x0a0a;
    Card_Fill();
}

/* Card_ShowQuestion FONT:03d0
   The question's page (cs:067f: English, French and German, four rows each), then in each language's
   gap the column letter ('A' + cs:11c0) and the row number (cs:11c1 + 1: the tens digit only when not
   0), one character each through cs:11bf. */
void Card_ShowQuestion(void)
{
    page_text(0x067f);
    SET_AH(0);
    SET_AL(cb(0x11c0));
    SET_AL(add8(AL, 0x41));
    cbw(0x11bf, AL);
    cell_text(1, 0x07);
    cell_text(5, 0x29);
    cell_text(9, 0x0a);
    R.ax = (uint16_t)(cb(0x11c1));
    SET_AL(inc8(AL));
    SET_BL(0x0a);
    div8(BL);
    cmp8(AL, 0);
    if (!R.zf) {
        SET_AL(add8(AL, 0x30));
        cbw(0x11bf, AL);
        cell_text(1, 0x11);
        cell_text(5, 0x3b);
        cell_text(9, 0x16);
    }
    R.ax = (uint16_t)(cb(0x11c1));
    SET_AL(inc8(AL));
    SET_BL(0x0a);
    div8(BL);
    SET_AL(AH);
    SET_AL(add8(AL, 0x30));
    cbw(0x11bf, AL);
    cell_text(1, 0x12);
    cell_text(5, 0x3c);
    cell_text(9, 0x17);
}

/* Card_Sprite FONT:04d4
   Draws the picture the variables describe: cs:2560 the source (four planes one after the other, each
   cs:2562 bytes x cs:2564 rows), at x cs:255e, y cs:255c from A000:cs:2566. ES = DS. */
void Card_Sprite(void)
{
    R.ax = R.ds;
    R.es = R.ax;
    R.cx = cw(0x2564);
    R.dx = cw(0x2562);
    R.si = cw(0x2560);
    R.ax = cw(0x255e);
    R.bx = cw(0x255c);
    R.di = R.bx;
    Card_SpriteFar();   /* push cs; call near: the callee returns far */
}

/* Card_SpriteFar FONT:04f7
   AX = x, BX = y, CL = rows, DL = bytes a row, DS:SI = the four planes. The screen address y * 80 + x / 8
   + [cs:2566] (cs:254f), the shift x & 7; [cs:2551] = one plane's size, [cs:254c] = rows - 1 (as the
   original, the last row is never drawn), [cs:254d] = the width. A mask of the four planes ORed together
   (Card_BuildMask) is ANDed out of the screen (Card_DrawMask), then each plane ORed into its own. Each
   output byte takes the 16 bits of a source byte and the next shifted right by the shift and keeps the
   low 8: the picture lands 8 - shift pixels left of x (as the original; its positions allow for it). */
void Card_SpriteFar(void)
{
    uint16_t ax = R.ax, dx = R.dx;
    uint32_t p = (uint32_t)0x50 * R.bx;
    R.ax = (uint16_t)p, R.dx = (uint16_t)(p >> 16);
    R.cf = R.dx != 0;
    R.di = R.ax;
    R.dx = dx, R.ax = ax;
    R.ax = shr16(R.ax, 3);
    R.di = add16(R.di, R.ax);
    R.di = add16(R.di, cw(0x2566));
    R.ax = ax;
    R.ax = logic16(R.ax & 7);
    SET_CH(AL);
    R.ax = cw(0x2556);
    R.es = R.ax;
    SET_AL(CL);
    R.ax = mul8(AL, DL);
    cww(0x2551, R.ax);
    SET_CL(dec8(CL));
    cbw(0x254c, CL);
    cww(0x254d, R.dx);
    cww(0x254f, R.di);
    SET_CL(CH);
    {
        uint16_t sdx = R.dx, scx = R.cx, sdi = R.di, ssi = R.si;
        R.dx = 0x3ce, R.ax = 0x0005;
        io_out16(R.dx, R.ax);
        Card_BuildMask();
        R.si = ssi, R.di = sdi, R.cx = scx, R.dx = sdx;
    }
    {
        uint16_t sdx = R.dx, scx = R.cx, sdi = R.di, ssi = R.si;
        Card_DrawMask();
        R.dx = 0x3ce, R.ax = 0x0005;
        io_out16(R.dx, R.ax);
        R.dx = 0x3ce, R.ax = 0x1003;   /* function OR */
        io_out16(R.dx, R.ax);
        R.si = ssi, R.di = sdi, R.cx = scx, R.dx = sdx;
    }
    SET_DL(1);
    do {
        uint16_t sdx = R.dx;
        SET_AH(DL);
        R.dx = 0x3c4;
        SET_AL(2);
        io_out16(R.dx, R.ax);        /* map mask: this plane only */
        R.di = cw(0x254f);
        SET_DH(cb(0x254c));
        SET_CH(cb(0x254d));
        uint16_t ssi = R.si;
        do {
            R.bx = R.si;
            R.bp = R.di;
            R.bx = add16(R.bx, cw(0x254d));
            R.bp = add16(R.bp, 0x50);
            SET_DL(CH);
            do {
                R.ax = mw(R.ds, R.si);
                R.si = (uint16_t)(R.si + 2 * step());
                R.ax = (uint16_t)(R.ax << 8 | R.ax >> 8);
                R.ax = shr16(R.ax, CL);
                SET_AH(vmem_rd8(R.es, R.di));   /* the latches loaded */
                vmem_wr8(R.es, R.di, AL);
                R.di = (uint16_t)(R.di + step());
                R.si = dec16(R.si);
                SET_DL(dec8(DL));
            } while (!R.zf);
            R.si = R.bx;
            R.di = R.bp;
            SET_DH(dec8(DH));
        } while (!R.zf);
        R.si = ssi;
        R.si = add16(R.si, cw(0x2551));
        R.dx = sdx;
        R.cf = (DL & 0x80) != 0;
        SET_DL((uint8_t)(DL << 1));
        SET_DL(logic8(DL & 0x0f));
    } while (!R.zf);
}

/* Card_CopyBox FONT:05b3
   The symbol box from A000:8000 (off screen) to the screen at row 179, byte 27 (x 216): 162 rows of 25
   bytes, all four planes at once through the latches (write mode 1). ES is left at A000. */
void Card_CopyBox(void)
{
    R.si = 0x8000, R.di = 0x380b;
    R.dx = 0x3c4, R.ax = 0x0f02;
    io_out16(R.dx, R.ax);
    uint16_t ds = R.ds;
    R.ax = 0xa000;
    R.ds = R.es = R.ax;
    R.dx = 0x3ce, R.ax = 0x0105;
    io_out16(R.dx, R.ax);
    R.cx = 0xa2;
    do {
        uint16_t scx = R.cx, ssi = R.si, sdi = R.di;
        for (R.cx = 0x19; R.cx; R.cx--) {
            vmem_wr8(R.es, R.di, vmem_rd8(R.ds, R.si));
            R.si = (uint16_t)(R.si + step());
            R.di = (uint16_t)(R.di + step());
        }
        R.di = sdi, R.si = ssi;
        R.di = add16(R.di, 0x50);
        R.si = add16(R.si, 0x50);
        R.cx = (uint16_t)(scx - 1);
    } while (R.cx);
    R.dx = 0x3ce, R.ax = 0x0005;
    io_out16(R.dx, R.ax);
    R.ds = ds;
}

/* Card_DrawMask FONT:05ee
   The mask at cs:11c2 ANDed out of all four planes (function AND, the CPU byte inverted), with the
   shift CL, [cs:254c] rows of [cs:254d] bytes from A000:[cs:254f]. */
void Card_DrawMask(void)
{
    R.dx = 0x3ce, R.ax = 0x0005;
    io_out16(R.dx, R.ax);
    R.dx = 0x3c4, R.ax = 0x0f02;
    io_out16(R.dx, R.ax);
    R.dx = 0x3ce, R.ax = 0x0803;   /* function AND */
    io_out16(R.dx, R.ax);
    uint16_t ds = R.ds;
    R.ds = SEG_OVL;
    R.di = cw(0x254f);
    SET_DH(cb(0x254c));
    R.si = 0x11c2;
    SET_CH(cb(0x254d));
    do {
        R.bx = R.si;
        R.bp = R.di;
        R.bx = add16(R.bx, cw(0x254d));
        R.bp = add16(R.bp, 0x50);
        SET_DL(CH);
        do {
            SET_AH(vmem_rd8(R.es, R.di));   /* the latches loaded */
            R.ax = mw(R.ds, R.si);
            R.si = (uint16_t)(R.si + 2 * step());
            R.ax = (uint16_t)(R.ax << 8 | R.ax >> 8);
            R.ax = shr16(R.ax, CL);
            SET_AL(~AL);
            vmem_wr8(R.es, R.di, AL);
            R.di = (uint16_t)(R.di + step());
            R.si = dec16(R.si);
            SET_DL(dec8(DL));
        } while (!R.zf);
        R.di = R.bp;
        R.si = R.bx;
        SET_DH(dec8(DH));
    } while (!R.zf);
    R.ds = ds;
}

/* Card_BuildMask FONT:0640
   The mask: for [cs:254c] rows of [cs:254d] bytes from DS:SI, each byte the OR of the four planes'
   bytes ([cs:2551] apart), into cs:11c2, then two zero bytes. DS is left at the overlay's. */
void Card_BuildMask(void)
{
    uint16_t si = R.si, di = R.di, es = R.es;
    R.ds = SEG_OVL;
    R.es = R.ds;
    R.di = 0x11c2;
    R.bx = cw(0x2551);
    SET_CL(cb(0x254c));
    SET_CH(0);
    do {
        uint16_t scx = R.cx;
        SET_CL(cb(0x254d));
        do {
            SET_AL(mb(R.ds, R.si));
            R.si = (uint16_t)(R.si + step());
            R.dx = R.si;
            R.si = dec16(R.si);
            R.si = add16(R.si, R.bx);
            SET_AL(logic8(AL | cb(R.si)));
            R.si = add16(R.si, R.bx);
            SET_AL(logic8(AL | cb(R.si)));
            R.si = add16(R.si, R.bx);
            SET_AL(logic8(AL | cb(R.si)));
            mbw(R.es, R.di, AL);
            R.di = (uint16_t)(R.di + step());
            R.si = R.dx;
        } while (--R.cx);
        R.cx = (uint16_t)(scx - 1);
    } while (R.cx);
    SET_AL(0);
    mbw(R.es, R.di, 0);
    R.di = (uint16_t)(R.di + step());
    mbw(R.es, R.di, 0);
    R.di = (uint16_t)(R.di + step());
    R.es = es, R.di = di, R.si = si;
}
