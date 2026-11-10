/* The race set-up (docs/re/race-setup.md): once per race, before its first frame, the program loads
   the track's files, sets every vehicle record to its start, expands the map into the two blocks of
   graphic-tile words and splits the vehicles' pictures, completing the turn of directions by mirroring.
   The vehicle records are 164h bytes apart from 193c:124c (vehicles 0..3 at record offsets 0, 164h,
   2c8h, 42ch); the fields named here are described in docs/re/race-setup.md. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* A segment operand of the program: relocated by the load segment like every segment constant of the
   image (MZ relocations), so the buffer the code calls 1b78 is at 2b78 in memory with the image at
   1000h. The notes use the operands' numbering (docs/re/race-setup.md). */
#define LSEG(raw) ((uint16_t)(SEG_CODE + (raw)))

enum { REC = 0x164, REC_LAST = 0x42c };

/* int 21h 3Dh with AL = C0h on the name at DS:DX: BX = AX = the handle or the error, CF from DOS. */
static void open_into_bx(uint16_t name)
{
    R.dx = name;
    R.ax = 0x3dc0;
    mach_int(0x21);
    R.bx = R.ax;
}

/* Reads up to FFFFh bytes (the whole file) into seg:dst from the handle in BX, then closes it. */
static void read_all_close(uint16_t seg, uint16_t dst)
{
    R.dx = dst;
    R.cx = 0xffff;
    R.ax = seg;
    R.ds = seg;
    SET_AH(0x3f);
    mach_int(0x21);
    SET_AH(0x3e);
    mach_int(0x21);
}

/* Race_Setup 1000:37fc
   The original starts with 255 nops (37fc..38fa), code blanked in place; they have no effect. Then:
   [2630] = 0, the smoothness [263a] at least 1, the CRTC base, the grid slots of the four vehicles
   ([137c + record] = 3, 2, 1, 0), [2682] = FFFFh; the track's files and the vehicle records (each step's
   carry ends the set-up as a failure); the fade out, the screen cleared and the overscan colour set to 0;
   for data set GAME1 ([042e] = '1') on themes 2 and 3 the map entries of a range of graphic tiles get
   bit 15 (40h..13Ch on theme 2, 1..51h on theme 3: the tiles drawn over the vehicles), over the first
   4706h words of each map block; the first picture, the theme's palette, the fade in and the driver's
   AH = 7. Out: CF clear; on a failure the engines silenced, the driver's AH = 8 and 6, AX = 1, CF set. */
void Race_Setup(void)
{
    mww(R.ds, 0x2630, 0);
    R.zf = mw(R.ds, 0x263a) == 0, R.sf = (mw(R.ds, 0x263a) & 0x8000) != 0;
    if (R.zf) mww(R.ds, 0x263a, 1);
    R.cf = false;   /* cmp [263a], 0 */
    Sys_ReadCrtcBase();
    mww(R.ds, 0x137c, 3);
    mww(R.ds, 0x14e0, 2);
    mww(R.ds, 0x1644, 1);
    mww(R.ds, 0x17a8, 0);
    mww(R.ds, 0x2682, 0xffff);
    Track_SetNames();
    Race_LoadTrack();
    if (R.cf) goto fail;
    Track_LoadColDir();
    if (R.cf) goto fail;
    Gfx_LoadBits();
    if (R.cf) goto fail;
    Car_LoadGfx();
    if (R.cf) goto fail;
    Track_LoadTiles();
    if (R.cf) goto fail;
    Pal_FadeOut();

    for (uint16_t i = 0; i < 0x7d00; i++) mww(SEG_VGA, (uint16_t)(2 * i), 0);   /* rep stosw */
    R.di = 0xfa00;
    R.ax = 0;
    R.cx = 0;
    SET_AH(0x10);   /* set the overscan (border) colour: BH = 0 */
    SET_AL(1);
    SET_BH(0);
    mach_int(0x10);

    {
        uint16_t ds = R.ds, es = R.es;
        R.ax = SEG_DATA;
        R.ds = SEG_DATA;
        uint8_t theme = d8(0x28bf);
        /* the flags the compares leave for the call below */
        if (d8(0x042e) != '1') R.cf = d8(0x042e) < '1', R.zf = false;
        else if (theme != 2 && theme != 3) R.cf = theme < 2, R.zf = false;
        else R.cf = false, R.zf = true;   /* the block loop's last cmp bx, ax */
        if (d8(0x042e) == '1' && (theme == 2 || theme == 3)) {
            for (uint16_t seg = LSEG(0x3b78);; seg = LSEG(0x4478)) {
                R.ax = seg;
                for (uint16_t i = 0; i < 0x4706; i++) {
                    uint16_t w = mw(seg, (uint16_t)(2 * i));
                    R.ax = w;
                    if (theme == 2 ? ((int16_t)w <= 0x13c && (int16_t)w >= 0x40) : ((int16_t)w <= 0x51 && w != 0))
                        R.ax = w | 0x8000;
                    mww(seg, (uint16_t)(2 * i), R.ax);
                }
                R.si = R.di = 0x8e0c;
                R.cx = 0;
                R.bx = seg;
                R.ax = LSEG(0x4478);   /* the test for the second block: bx = ds against 4478h */
                if (seg == LSEG(0x4478)) break;
            }
        }
        R.ds = ds, R.es = es;
    }
    Race_DrawFrame();
    Pal_LoadRound();
    if (R.cf) goto fail;
    Pal_FadeIn();
    SET_AH(7);
    mach_drv();
    R.cf = false;
    return;
fail:
    Snd_SilenceEngines();
    SET_AH(8);
    mach_drv();
    SET_AH(6);
    mach_drv();
    R.ax = 1;
    R.cf = true;
}

/* The handling words of a vehicle record from the theme's row of nine words at 193c:252a + 12h
   (theme - 1); see Race_LoadTrack. */
static uint16_t lodsw(uint16_t *si)
{
    uint16_t v = d16(*si);
    *si = (uint16_t)(*si + 2);
    return v;
}

/* The race's state from its files (Race_LoadTrack, below; all registers kept, the input slots re-filled
   at the end, 1000:2d00):
   - the race globals reset (camera speeds [264e] [2650] = 8, the banner and head-to-head counters, the
     per-track word [26c8] from the table at 193c:2419 by track, [1096] (Esc) = 0, [2682] = FFFFh);
   - STRT_POS.BIN into 193c:1eab, CHEATS.BIN into 1bdb, the track's ROUNDnm.MAP into 2963 (whole files);
     [2652] = the largest byte of the map's second kilobyte (its progress numbers), [2654] = half of it;
   - the start grid (3d53): the track's four bytes in STRT_POS (at (theme - 1) * 16 + (track - 1) * 4,
     8-bit) give x + 14h and y - 0Ah; each vehicle adds 1Ah to x for bit 0 of its grid slot and to y for
     bit 1; the camera [264a]/[2646], [264c]/[2648] = the start less FAh, plus C00h when not above 0;
   - the handling (3f19): from the theme's nine words at 252a + 12h (theme - 1): top speed [129c],
     reverse [12a0], acceleration [12a2], braking [12a4], drag [12a6], grip [127c] [127e], then (4134)
     the slide rates [28c2] [28c4]. With the handicap option (193c:08a2, read as cs:9c62) a vehicle whose
     handicap byte [2668 + 2i] has bit 7 loses speed and acceleration by its level; else the computer
     cars (not vehicle 0) get a level from the driver's table at 23dc (or, in the first race, from their
     grid place), adjusted by theme and race number [28c1], and a penalty from the table at 2462 by race;
   - who drives (4134): the presence and computer flags of the records from the game type [2656] (1: one
     player against three, 2: head to head) and the control types [2658..265e] (6 = computer);
   - every record's motion state cleared (4220): the progress list [2670 + 2i] = 9 * [2652] (8-bit),
     [2678 + 2i] = the record offset, the state [12ae] = 0Ah (waiting for the start), the lists at
     [12fd] (30h words) and [135d] (0Fh words) = FFFFh;
   - ROUNDnBR.CT into 6d78:0000 and (446a) the 32 x 32 map expanded through it: each map byte's low six
     bits pick a 72-byte composition (6 rows of 6 graphic-tile words), written as a 6 x 6 block into the
     192 x 192 grid of words, rows 0-95 at 3b78:0000 and 96-191 at 4478:0000, 384 bytes a row.
   CF set if a file does not open; the close after it uses the error code as a handle, as the original. */

/* Race_LoadTrack 1000:3c09 */
void Race_LoadTrack(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = SEG_DATA;
    d16w(0x264e, 8);
    d16w(0x2650, 8);
    d16w(0x26c6, 0);
    d16w(0x2911, 0);
    d16w(0x2913, 0);
    d16w(0x26c4, 1);
    d16w(0x26b4, 4);
    d16w(0x26b6, 4);
    d16w(0x26b8, 1);
    d16w(0x26ba, 0);
    {
        uint16_t bx = (uint16_t)(d8(0x28c0) - 1);
        bx = (uint16_t)(bx + bx);
        R.cf = (uint32_t)bx + 0x2419 > 0xffff;   /* add bx, 2419h: the flags the first open sees */
        bx = (uint16_t)(bx + 0x2419);
        flags_zs16(bx);
        d16w(0x26c8, d16(bx));
    }
    d16w(0x26d1, 0);
    d16w(0x26d5, 0);
    d16w(0x26bc, 0);
    d16w(0x26be, 0);
    d16w(0x26c0, 0);
    d16w(0x26c2, 0);
    d16w(0x2633, 0);
    d16w(0x2635, 0);
    d16w(0x26cc, 0x64);
    d16w(0x2917, 0);
    d16w(0x2915, 0);
    d16w(0x2919, 0);
    d16w(0x291b, 3);
    d8w(0x1096, 0);
    d16w(0x291d, 0);
    d16w(0x2682, 0xffff);

    open_into_bx(0x11cd);   /* STRT_POS.BIN */
    if (R.cf) goto fail;
    read_all_close(SEG_DATA, 0x1eab);
    open_into_bx(0x11c2);   /* CHEATS.BIN */
    if (R.cf) goto fail;
    read_all_close(SEG_DATA, 0x1bdb);
    open_into_bx(0x11a0);   /* ROUNDnm.MAP */
    if (R.cf) goto fail;
    read_all_close(SEG_DATA, 0x2963);

    {
        uint8_t top = 0;
        for (uint16_t i = 0; i < 0x400; i++)
            if (d8((uint16_t)(0x2d63 + i)) >= top) top = d8((uint16_t)(0x2d63 + i));
        d16w(0x2652, top);
        d16w(0x2654, (uint16_t)(top >> 1));
    }

    /* 3d53: the start grid */
    d16w(0x1380, 1);
    d16w(0x14e4, 1);
    d16w(0x1648, 1);
    d16w(0x17ac, 1);
    d16w(0x137e, 0);
    d16w(0x14e2, 0);
    d16w(0x1646, 0);
    d16w(0x17aa, 0);
    {
        uint8_t at = (uint8_t)((uint8_t)((d8(0x28bf) - 1) << 4) + (uint8_t)((d8(0x28c0) - 1) << 2));
        uint16_t si = (uint16_t)(0x1eab + at);
        uint16_t x = (uint16_t)(lodsw(&si) + 0x14);
        for (uint16_t r = 0; r <= REC_LAST; r = (uint16_t)(r + REC)) {
            uint16_t v = (uint16_t)(x + ((d16((uint16_t)(0x137c + r)) & 1) ? 0x1a : 0));
            d16w((uint16_t)(0x125c + r), v);
            d16w((uint16_t)(0x125e + r), v);
        }
        uint16_t cam = (uint16_t)(x - 0xfa);
        if ((int16_t)cam <= 0) cam = (uint16_t)(cam + 0xc00);
        d16w(0x264a, cam);
        d16w(0x2646, cam);
        uint16_t y = (uint16_t)(lodsw(&si) - 0x0a);
        for (uint16_t r = 0; r <= REC_LAST; r = (uint16_t)(r + REC)) {
            uint16_t v = (uint16_t)(y + ((d16((uint16_t)(0x137c + r)) & 2) ? 0x1a : 0));
            d16w((uint16_t)(0x1268 + r), v);
            d16w((uint16_t)(0x126a + r), v);
        }
        cam = (uint16_t)(y - 0xfa);
        if ((int16_t)cam <= 0) cam = (uint16_t)(cam + 0xc00);
        d16w(0x264c, cam);
        d16w(0x2648, cam);
    }
    for (uint16_t i = 0; i < 3; i++) {
        uint16_t b = (uint16_t)(0x2684 + 0x10 * i);   /* three groups of 0, 0, 0, 0, 1, 1, 1, 1 */
        d16w((uint16_t)(b + 2), 0);
        d16w((uint16_t)(b + 4), 0);
        d16w((uint16_t)(b + 6), 0);
        d16w(b, 0);
        d16w((uint16_t)(b + 8), 1);
        d16w((uint16_t)(b + 10), 1);
        d16w((uint16_t)(b + 12), 1);
        d16w((uint16_t)(b + 14), 1);
    }
    d16w(0x27b5, 1);
    if (d16(0x2656) == 2) d16w(0x27b5, 0);

    /* 3f19: the handling */
    {
        uint16_t row = (uint16_t)(0x252a + (uint8_t)(d8(0x28bf) - 1) * 0x12);
        uint8_t race = d8(0x28c1);
        uint16_t di = 0;
        uint16_t r = 0;
        for (;;) {
            uint16_t si = row;
            if (c8(0x9c62) != 0) {
                /* the handicap option: level dx = (handicap & 7Fh) + 1 when bit 7 is set */
                uint16_t dx = 0;
                uint16_t h = d16((uint16_t)(0x2668 + di));
                if (h & 0x80) dx = (uint16_t)((h + 1) & 0x7f);
                uint16_t ax = lodsw(&si);
                if (dx) ax = (uint16_t)(ax - (uint16_t)(0xc0 - (uint16_t)((dx - 1) << 6)));
                d16w((uint16_t)(0x129c + r), ax);
                d16w((uint16_t)(0x12a0 + r), (uint16_t)(lodsw(&si) + 0x32));
                ax = lodsw(&si);
                if (dx) ax = (uint16_t)(ax - (uint16_t)(0x0c - (uint16_t)((dx - 1) << 2)));
                d16w((uint16_t)(0x12a2 + r), ax);
                d16w((uint16_t)(0x12a4 + r), lodsw(&si));
                d16w((uint16_t)(0x12a6 + r), lodsw(&si));
                d16w((uint16_t)(0x127c + r), (uint16_t)(lodsw(&si) + d16(0x24e0)));
                d16w((uint16_t)(0x127e + r), (uint16_t)(lodsw(&si) + d16(0x24e0)));
                if (r == REC_LAST) break;
                r = (uint16_t)(r + REC);
                di = (uint16_t)(di + 2);
                continue;
            }
            /* the computer cars' level cx */
            uint16_t cx = d16((uint16_t)(0x23dc + 2 * d16((uint16_t)(0x2668 + di))));
            if (r == 0) {
                cx = 0;
            } else {
                if ((int8_t)race <= 0) {   /* the first race: by grid place */
                    cx = 0;
                    if (r != REC) {
                        cx = (uint16_t)(cx + 6);
                        if (r != REC_LAST) cx = (uint16_t)(cx + 6);
                    }
                }
                cx = (uint16_t)(cx - 0x0f + race);
                uint8_t theme = d8(0x28bf);
                if (theme == 7) cx = (uint16_t)(cx + 4);
                if (theme == 8) cx = (uint16_t)(cx + 8);
                if (theme == 2) cx = (uint16_t)(cx + 7);
                if (race == 8) cx = (uint16_t)(cx + 3);
                if (race >= 0x13) cx = (uint16_t)(cx + 8);
                if (race == 0x15) cx = (uint16_t)(cx - 1);
                if (race == 0x16) cx = (uint16_t)(cx - 8);
                if (race == 0x17) cx = (uint16_t)(cx + 0x14);
                if (race == 0x18) cx = (uint16_t)(cx - 5);
            }
            uint16_t ax = (uint16_t)(lodsw(&si) + 11 * cx);
            if (r != 0) ax = (uint16_t)(ax - d16((uint16_t)(0x2462 + 2 * race)));
            d16w((uint16_t)(0x129c + r), ax);
            d16w((uint16_t)(0x12a0 + r), (uint16_t)(lodsw(&si) + 4 * cx + 0x32));
            d16w((uint16_t)(0x12a2 + r), (uint16_t)(lodsw(&si) + cx));
            if (r != 0 && d16(0x2656) == 2 && c8(0x9c62) == 0 && race >= 0x12) {
                uint16_t cut = (uint16_t)(d16((uint16_t)(0x12a2 + r)) >> 2);
                if (race != 0x17) cut = (uint16_t)(cut >> 2);
                d16w((uint16_t)(0x12a2 + r), (uint16_t)(d16((uint16_t)(0x12a2 + r)) - cut));
            }
            d16w((uint16_t)(0x12a4 + r), (uint16_t)(lodsw(&si) + cx + 0x28));
            d16w((uint16_t)(0x12a6 + r), lodsw(&si));
            ax = (uint16_t)(lodsw(&si) + 2 * cx);
            ax = (uint16_t)(ax + (r != 0 ? 0x28 : d16(0x24e0)));
            d16w((uint16_t)(0x127c + r), ax);
            ax = (uint16_t)(lodsw(&si) + 3 * cx);
            ax = (uint16_t)(ax + (r != 0 ? 0x28 : d16(0x24e0)));
            d16w((uint16_t)(0x127e + r), ax);
            if (r == REC_LAST) break;
            r = (uint16_t)(r + REC);
            di = (uint16_t)(di + 2);
            if (race == 0x17) {   /* as the original: vehicle 0 loses this after each computer car */
                d16w(0x129c, (uint16_t)(d16(0x129c) - 0x4b));
                d16w(0x12a2, (uint16_t)(d16(0x12a2) - 2));
            }
        }
        /* 4134: past the seven words, the slide rates */
        uint16_t si = (uint16_t)(row + 0x0e);
        d16w(0x28c2, lodsw(&si));
        d16w(0x28c4, lodsw(&si));
    }

    /* who drives */
    if (d16(0x2656) != 2) {
        d16w(0x27b5, 1);
        d16w(0x2660, 0);
        d16w(0x2662, REC);
    } else {
        d16w(0x27b5, 0);
    }
    d16w(0x124e, 1);
    d16w(0x13b2, 1);
    d16w(0x1516, 1);
    d16w(0x167a, 1);
    d16w(0x12eb, 0);
    d16w(0x144f, 1);
    d16w(0x15b3, 1);
    d16w(0x1717, 1);
    d16w(0x265c, 6);
    d16w(0x265e, 6);
    if (d16(0x2656) != 1) {
        d16w(0x144f, 0);
        d16w(0x1516, 0);
        d16w(0x167a, 0);
    } else {
        d16w(0x265a, 6);
    }
    if (d8(0x28bf) == 9) {
        d16w(0x13b2, 0);
        d16w(0x1516, 0);
        d16w(0x167a, 0);
        d16w(0x27b5, 1);
    }
    for (uint16_t i = 0; i < 4; i++)
        if (d16((uint16_t)(0x2658 + 2 * i)) == 6) d16w((uint16_t)(0x12eb + d16((uint16_t)(0x2660 + 2 * i))), 1);

    /* 4220: every record's motion state */
    for (uint16_t r = 0, si = 0;; r = (uint16_t)(r + REC), si = (uint16_t)(si + 2)) {
        d16w((uint16_t)(0x2670 + si), (uint16_t)(9 * (uint8_t)d16(0x2652)));
        d16w((uint16_t)(0x2678 + si), r);
        static const uint16_t zero[] = {
            0x12ef, 0x1278, 0x1272, 0x1270, 0x1276, 0x1274, 0x1258, 0x125a, 0x1264, 0x1266, 0x127a, 0x1282,
            0x1284, 0x1286, 0x1288, 0x128a};
        d16w((uint16_t)(0x12ef + r), 0);
        d16w((uint16_t)(0x129e + r), d16((uint16_t)(0x129c + r)));
        for (unsigned k = 1; k < sizeof zero / sizeof zero[0]; k++) d16w((uint16_t)(zero[k] + r), 0);
        d16w((uint16_t)(0x128e + r), 0xffe2);
        d16w((uint16_t)(0x1290 + r), 0x14);
        d16w((uint16_t)(0x1292 + r), 0x1e);
        d16w((uint16_t)(0x1294 + r), 0xffec);
        static const uint16_t zero2[] = {
            0x1296, 0x12a8, 0x12ac, 0x12b0, 0x12b2, 0x12b4, 0x12b6, 0x12c2, 0x12be, 0x12c0, 0x12c4, 0x12c6,
            0x12c8, 0x12ca, 0x12cc, 0x12ce, 0x12d0, 0x12d2, 0x12d4, 0x12d6};
        for (unsigned k = 0; k < sizeof zero2 / sizeof zero2[0]; k++) d16w((uint16_t)(zero2[k] + r), 0);
        d16w((uint16_t)(0x12d8 + r), 1);
        d16w((uint16_t)(0x12da + r), 0);
        d16w((uint16_t)(0x12dc + r), 0);
        d16w((uint16_t)(0x12de + r), 0);
        d8w((uint16_t)(0x12e0 + r), 0);
        d16w((uint16_t)(0x12e1 + r), 0);
        d16w((uint16_t)(0x12e3 + r), 0);
        d16w((uint16_t)(0x12e5 + r), 0);
        d16w((uint16_t)(0x12e7 + r), 0);
        d16w((uint16_t)(0x12e9 + r), 0);
        d16w((uint16_t)(0x12ed + r), 3);
        d16w((uint16_t)(0x12f1 + r), d16((uint16_t)(0x125c + r)));
        d16w((uint16_t)(0x12f3 + r), d16((uint16_t)(0x1268 + r)));
        d16w((uint16_t)(0x12f5 + r), 0);
        d16w((uint16_t)(0x12f7 + r), 0);
        d8w((uint16_t)(0x137b + r), 0);
        d16w((uint16_t)(0x1382 + r), 0);
        d16w((uint16_t)(0x1384 + r), 0);
        d16w((uint16_t)(0x124c + r), d16((uint16_t)(0x124e + r)));
        d16w((uint16_t)(0x1386 + r), 0);
        d16w((uint16_t)(0x1388 + r), 0);
        d16w((uint16_t)(0x138e + r), 0);
        d16w((uint16_t)(0x138a + r), 1);
        d16w((uint16_t)(0x138c + r), 0);
        d16w((uint16_t)(0x1390 + r), 0);
        d16w((uint16_t)(0x1392 + r), 0);
        d16w((uint16_t)(0x12ae + r), 0x0a);
        d16w((uint16_t)(0x1394 + r), 0);
        d16w((uint16_t)(0x1398 + r), 0);
        d16w((uint16_t)(0x139c + r), 0);
        d16w((uint16_t)(0x13a0 + r), 0);
        d16w((uint16_t)(0x13a2 + r), 0);
        d16w((uint16_t)(0x1396 + r), 0);
        d16w((uint16_t)(0x13a6 + r), 0);
        d16w((uint16_t)(0x12ae + r), 0x0a);
        d16w((uint16_t)(0x1250 + r), 0);
        uint8_t theme = d8(0x28bf);
        d16w((uint16_t)(0x129a + r), (theme == 7 || theme == 6) ? 2 : 3);
        for (uint16_t k = 0; k < 0x30; k++) d16w((uint16_t)(0x12fd + r + 2 * k), 0xffff);
        for (uint16_t k = 0; k < 0x0f; k++) d16w((uint16_t)(0x135d + r + 2 * k), 0xffff);
        if (r == REC_LAST) break;
    }
    if (d16(0x2656) == 2) {
        d16w(0x2678, d16(0x2660));
        d16w(0x267a, d16(0x2662));
    }

    /* 446a: the compositions and the map's expansion */
    /* the registers as the loops leave them for the open */
    R.ds = R.es = SEG_DATA;
    R.si = 8;
    R.di = 0x135d + REC_LAST + 2 * 0x0f;
    R.cx = 0;
    R.bx = REC_LAST;
    if (d16(0x2656) == 2) {
        R.bx = d16(0x2662);
        R.cf = false, R.zf = true;
    } else {
        R.cf = d16(0x2656) < 2, R.zf = false;
    }
    open_into_bx(0x120b);   /* ROUNDnBR.CT */
    if (R.cf) goto fail;
    read_all_close(LSEG(0x6d78), 0);
    {
        uint16_t si = 0x2963;
        for (uint16_t seg = LSEG(0x3b78);; seg = LSEG(0x4478)) {
            uint16_t di = 0;
            uint16_t col = 0;
            for (uint16_t n = 0; n < 0x200; n++) {
                uint16_t src = (uint16_t)((d8(si) & 0x3f) * 72);
                si++;
                for (int row = 0; row < 6; row++) {
                    for (int w = 0; w < 6; w++) {
                        mww(seg, di, mw(LSEG(0x6d78), src));
                        di = (uint16_t)(di + 2);
                        src = (uint16_t)(src + 2);
                    }
                    if (row < 5) di = (uint16_t)(di + 0x174);
                }
                di = (uint16_t)(di - 0x780);   /* back to the top row, 12 bytes on */
                if (++col >= 0x20) {
                    col = 0;
                    di = (uint16_t)(di + 0x780);
                }
            }
            if (seg == LSEG(0x4478)) break;
        }
    }
    R = saved;
    R.cf = false;
    Input_SetSlots();
    return;
fail:
    SET_AH(0x3e);
    mach_int(0x21);
    R = saved;
    R.cf = true;
    Input_SetSlots();
}

/* Car_LoadGfx 1000:4611
   The vehicles' pictures ROUNDnBR.VH0.. (series name at 193c:11f3) through the packed-file loader into
   1b78:0000, then split: theme 9 (40 x 40 frames) 3840h bytes to 4d78:0000 and the next 1F40h to
   5d78:0000, the mirrored frames by 1000:46f6; the other themes (24 x 24 frames) 1440h bytes to 4d78:0000
   and 1B00h to 5d78:0000, mirrors by 1000:4696, and on theme 8 the first 1400h bytes of 5d78 also into
   193c:5ee3. All registers kept, CF clear. */
void Car_LoadGfx(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = SEG_DATA;
    R.si = 0x11f3;
    R.dx = LSEG(0x1b78);
    Unpack_LoadSeries();
    R.ds = SEG_DATA;
    uint16_t si = 0;
    if (d8(0x28bf) == 9) {
        for (uint16_t i = 0; i < 0x3840; i++, si++) mbw(LSEG(0x4d78), i, mb(LSEG(0x1b78), si));
        for (uint16_t i = 0; i < 0x1f40; i++, si++) mbw(LSEG(0x5d78), i, mb(LSEG(0x1b78), si));
        Car_MirrorFrames40();
    } else {
        for (uint16_t i = 0; i < 0x1440; i++, si++) mbw(LSEG(0x4d78), i, mb(LSEG(0x1b78), si));
        for (uint16_t i = 0; i < 0x1b00; i++, si++) mbw(LSEG(0x5d78), i, mb(LSEG(0x1b78), si));
        Car_MirrorFrames24();
        R.zf = d8(0x28bf) == 8, R.sf = (uint8_t)(d8(0x28bf) - 8) >= 0x80;   /* cmp [28bf], 8 */
        if (d8(0x28bf) == 8)
            for (uint16_t i = 0; i < 0x1400; i++) d8w((uint16_t)(0x5ee3 + i), mb(LSEG(0x5d78), i));
    }
    bool zf = R.zf, sf = R.sf;
    R = saved;
    R.zf = zf, R.sf = sf;
    R.cf = false;
}

/* The two mirrorings in 4d78 (DS = ES = 4d78), with the frame size n (24 or 40 pixels): from the eight
   frames drawn, n rows of n bytes each, ending before `src0`, the first pass copies whole rows from the
   last backwards (frames and rows in reverse order: flipped top to bottom) to `dst0`; the second and
   third passes copy 7 and then 8 frames byte by byte, each row right to left (flipped left to right),
   continuing from where the first pass left SI after `skip`. Out: AH = the segment's high byte, AL = 0, CX = 0, DI and SI past
   the passes, DS = ES = 4d78; CF from the last frame step. */
static void mirror_frames(uint16_t src0, uint16_t dst0, uint8_t n, uint16_t skip)
{
    const uint16_t seg = LSEG(0x4d78);
    uint16_t si = src0, di = dst0;
    for (int f = 0; f < 8; f++)
        for (int row = 0; row < n; row++) {
            for (int k = 0; k < n; k++) mbw(seg, (uint16_t)(di + k), mb(seg, (uint16_t)(si + k)));
            si = (uint16_t)(si + n);
            di = (uint16_t)(di + n);
            si = (uint16_t)(si - 2 * n);
        }
    si = (uint16_t)(si + skip);
    bool cf = false;
    for (int pass = 0; pass < 2; pass++)
        for (int f = 0; f < (pass ? 8 : 7); f++) {
            for (int row = 0; row < n; row++) {
                for (int k = 0; k < n; k++) {
                    mbw(seg, di, mb(seg, si));
                    di++;
                    si = (uint16_t)(si + 1 - 2);
                }
                si = (uint16_t)(si + 2 * n);
            }
            uint16_t frame = (uint16_t)(n * n * 2);
            cf = si < frame;
            si = (uint16_t)(si - frame);
        }
    R.ax = (uint16_t)(seg & 0xff00);   /* mov ax, 4d78h, then AL counted down */
    R.cx = 0;
    R.si = si;
    R.di = di;
    R.ds = R.es = seg;
    R.cf = cf;
    R.zf = true, R.sf = false;   /* dec ch to 0 */
}

/* Car_MirrorFrames24 1000:4696
   The 24 x 24 frames: from the eight at 4d78:0000 the flipped copies from 1440h on (the first pass reads
   back from 11e8h, the last row of the eighth frame). DX = 0 at the end (DH and DL counted down). */
void Car_MirrorFrames24(void)
{
    mirror_frames(0x11e8, 0x1440, 0x18, 0x21ef);
    R.dx = 0;
}

/* Car_MirrorFrames40 1000:46f6
   The same for theme 9's 40 x 40 frames: the first pass from 31d8h, written from 3840h on. Only DL
   counts (DH kept). */
void Car_MirrorFrames40(void)
{
    mirror_frames(0x31d8, 0x3840, 0x28, 0x5e0f);
    SET_DL(0);
}
