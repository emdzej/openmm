/* The packed files (docs/formats/packed.md): COMPRESS.PI*, the tiles, the vehicles, BITSFILE.PH0 and the
   rest are read whole into 6d78:c000 and decoded into 6d78:0000 by an LZ decoder of the original's own
   design, then copied to their place. The decoder is ported register for register: the codes leave
   CX, BX and DX as they go, and its caller and the code-segment bytes it saves are compared with the
   original's. */
#include "cpu.h"
#include "gfx.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* Unpack_SwapState stores the FLAGS word (pushf into cs:3545): Unpack_Decode sets PF, AF and OF from
   the shift it has just made (R.fl, cpu.h), IF stays as the caller has it. */
#define s_flags_rest R.fl
static uint16_t flags_word(void) { return cpu_flags(); }
static void flags_load(uint16_t f) { cpu_set_flags(f); }

static bool parity8(uint8_t v)
{
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return !(v & 1);
}

/* `add al, al` (cin = 0) or `adc al, al` (cin = 1) on the control byte: the flag bit out in CF, and the
   whole FLAGS result, which Unpack_SwapState may store */
static uint8_t shift_control(uint8_t al, bool cin)
{
    uint8_t r = (uint8_t)(al << 1 | cin);
    R.cf = al & 0x80;
    R.zf = r == 0;
    R.sf = r & 0x80;
    s_flags_rest = (uint16_t)((R.fl & 0x0200) | 0x0002 | (parity8(r) ? FL_PF : 0) | (r & 0x10 ? FL_AF : 0) |
                              (((al ^ r) & 0x80) ? FL_OF : 0));
    return r;
}

/* Unpack_SwapState 1000:3533
   Exchanges AL with the byte cs:3544 and the FLAGS with the word cs:3545. The decoder calls it in pairs
   around each code, so that the control byte being shifted and the flags of its last shift survive the
   code's use of AL. */
void Unpack_SwapState(void)
{
    uint8_t al = AL;
    SET_AL(c8(0x3544));
    c8w(0x3544, al);
    uint16_t f = flags_word();
    flags_load(c16(0x3545));
    c16w(0x3545, f);
}

static void movsb_dec(void)
{
    movsb();
    R.cx--;
}

/* Unpack_Decode 1000:3333
   Decodes 6d78:c000 into 6d78:0000 (docs/formats/packed.md). A control byte gives eight flag bits,
   most significant first: 0 a literal byte, 1 a code. The codes copy from the output already written
   (back references, forward, backward, a repeated byte, a rising run) or long runs of literals; FFh
   ends. Returns DS = ES = 6d78, SI = 0, DI = DX = the decoded length, CL its low byte, BH and AL its
   high byte, BL = 0, AH = 6dh, CH as the last code left it; CF clear. */
void Unpack_Decode(void)
{
    R.ax = SEG_BACK;
    R.ds = R.es = SEG_BACK;
    R.si = 0xc000;
    R.di = 0;
    R.df = false;
    uint16_t start = R.di;   /* pushed; popped at the end code */
    uint8_t ctl = 0;
    bool code;

reload:   /* 1000:3401: a new control byte, a 1 shifted in behind its bits as the end marker */
    ctl = shift_control(mb(R.ds, R.si++), true);
    code = R.cf;
    for (;;) {
        if (!code) {
            movsb_dec();   /* a literal */
            ctl = shift_control(ctl, false);
            if (R.cf && R.zf) goto reload;   /* the marker: all eight bits used */
            code = R.cf;
            continue;
        }
        SET_AL(ctl);
        Unpack_SwapState();   /* the control byte and its flags put away */
    next_code:;
        uint8_t n = mb(R.ds, R.si);
        if (n >= 0x80) {
            if (n == 0xff) {   /* 1000:3345: the end */
                R.si = start;
                R.dx = R.di;
                R.bx = R.si;
                SET_CL((uint8_t)(DL - BL));
                bool borrow = DL < BL;
                uint8_t hi = (uint8_t)(DH - BH - borrow);
                R.cf = DH < (unsigned)BH + borrow;
                flags_zs8(hi);
                SET_AL(hi);
                SET_BH(hi);
                return;
            }
            /* 1000:33d6: a short back reference in one byte: 2..5 bytes from up to 37 back */
            uint8_t c = (uint8_t)(((n & 0x60) >> 5) + 1);
            SET_CL(c);
            uint16_t at = R.si;
            R.dx = R.di;
            R.bx = R.si;
            uint8_t v = (uint8_t)(~((n & 0x1f) + c));
            R.cf = (unsigned)v + DL > 0xff;
            SET_BL((uint8_t)(v + DL));
            bool carry = R.cf;
            R.cf = (unsigned)DH + 0xff + carry > 0xff;
            SET_BH((uint8_t)(DH + 0xff + carry));
            R.di = R.dx;
            R.si = R.bx;   /* (joins the two-byte reference after its dec si) */
            SET_CH(0);
            SET_CL(CL + 1);
            movsb_dec();
            rep_movsb();
            R.si = (uint16_t)(at + 1);
            Unpack_SwapState();
            ctl = AL;
        } else {
            R.si++;
            if (n >= 0x70) {
                /* 1000:3489: a rising run, each byte one more than the last: 2..16, or 7f and a count */
                uint8_t c = (uint8_t)(n - 0x70 - 0x0f);
                if (c == 0) c = mb(R.ds, R.si++);
                c = (uint8_t)(c + 0x11);
                SET_CH(c);
                R.di--;
                uint8_t v = mb(R.es, R.di);
                R.di++;
                do {
                    v++;
                    mbw(R.es, R.di, v);
                    R.di++;
                    SET_CH(CH - 1);
                } while (CH != 0);
                SET_AL(v);
                R.cf = false, R.zf = true, R.sf = false;
                Unpack_SwapState();
                ctl = AL;
            } else if (n >= 0x60) {
                /* 1000:34ac: a reversed copy: 3..18 bytes read backwards from up to 256 back */
                SET_CH((uint8_t)(n - 0x60 + 3));
                uint8_t b = mb(R.ds, R.si);
                uint16_t at = R.si;
                R.dx = R.di;
                R.bx = R.si;
                uint8_t v = (uint8_t)~b;
                unsigned s = (unsigned)v + DL + 1;
                SET_BL((uint8_t)s);
                bool carry = s > 0xff;
                SET_BH((uint8_t)(DH + 0xff + carry));
                R.di = R.dx;
                R.si = R.bx;
                do {
                    R.si--;
                    uint8_t x = mb(R.ds, R.si);
                    SET_AL(x);
                    mbw(R.es, R.di, x);
                    R.di++;
                    SET_CH(CH - 1);
                } while (CH != 0);
                R.si = (uint16_t)(at + 1);
                R.cf = false;
                flags_zs16(R.si);
                Unpack_SwapState();
                ctl = AL;
            } else if (n >= 0x50) {
                /* 1000:3357: a long back reference: 50..5e lo count, or 5f hi lo count; 4..259 bytes */
                if (n == 0x5f) {
                    SET_CH(mb(R.ds, R.si));
                    R.si++;
                } else {
                    SET_CH((uint8_t)(n - 0x50));
                }
                R.dx = R.di;
                R.bx = R.si;
                uint8_t lo = mb(R.ds, R.si);
                bool borrow = DL < lo;
                uint8_t l = (uint8_t)(DL - lo);
                R.si++;
                SET_CL(mb(R.ds, R.si));
                uint16_t at = R.si;
                R.dx = R.di;
                R.bx = R.si;
                SET_BL(l);
                R.cf = DH < (unsigned)CH + borrow;
                SET_BH((uint8_t)(DH - CH - borrow));
                R.di = R.dx;
                R.si = R.bx;
                R.si--;
                uint8_t count = CL;
                movsb_dec();
                movsb_dec();
                movsb_dec();
                SET_CL(count);
                SET_CH(0);
                R.cx++;
                rep_movsb();
                R.si = (uint16_t)(at + 1);
                Unpack_SwapState();
                ctl = AL;
            } else if (n >= 0x20) {
                /* 1000:3385: a back reference in two bytes: 3..18 bytes from up to 3 * 256 back */
                uint8_t k = (uint8_t)(n - 0x20);
                SET_CL((uint8_t)((k & 0x0f) + 2));
                SET_CH((uint8_t)~((k & 0x30) >> 4));
                uint8_t b = mb(R.ds, R.si);
                uint16_t at = R.si;
                R.dx = R.di;
                R.bx = R.si;
                uint8_t v = (uint8_t)~b;
                unsigned s = (unsigned)v + DL;
                SET_BL((uint8_t)s);
                unsigned t = (unsigned)DH + CH + (s > 0xff);
                SET_BH((uint8_t)t);
                R.cf = t > 0xff;
                R.di = R.dx;
                R.si = R.bx;
                R.si--;
                SET_CH(0);
                SET_CL(CL + 1);
                movsb_dec();
                rep_movsb();
                R.si = (uint16_t)(at + 1);
                Unpack_SwapState();
                ctl = AL;
            } else if (n >= 0x10) {
                /* 1000:345c: the last byte repeated: 2..16 times, or 1f and a count (17..272) */
                SET_CH(0);
                uint8_t c = (uint8_t)(n - 0x1f);
                if (c == 0) {
                    uint8_t b = mb(R.ds, R.si++);
                    R.cf = (unsigned)b + 0x11 > 0xff;
                    c = (uint8_t)(b + 0x11);
                    if (R.cf) SET_CH(CH + 1);
                } else {
                    R.cf = (unsigned)c + 0x11 > 0xff;
                    c = (uint8_t)(c + 0x11);
                }
                SET_CL(c);
                uint16_t at = R.si;
                R.dx = R.di;
                R.bx = R.di;
                R.si = R.bx;
                R.si--;
                movsb_dec();
                flags_zs16(R.cx);
                rep_movsb();
                R.si = at;
                Unpack_SwapState();
                ctl = AL;
            } else {
                /* 1000:34dd: literals in a run, then straight on to the next code (no flag bit):
                   00..0e: 8..22 bytes; 0f b: b + 30; 0f ff lo hi: a 16-bit count */
                SET_CH(0);
                uint8_t a = (uint8_t)(n - 0x0f);
                if (a != 0) {
                    SET_CL((uint8_t)(a + 0x17));
                    for (int k = 0; k < 7; k++) movsb_dec();
                    rep_movsb();
                    goto next_code;
                }
                uint8_t b = (uint8_t)(mb(R.ds, R.si++) + 1);
                if (b == 0) {
                    SET_CL(mb(R.ds, R.si++));
                    SET_CH(mb(R.ds, R.si++));
                } else {
                    unsigned s = (unsigned)b + 0x1d;
                    SET_CL((uint8_t)s);
                    if (s > 0xff) SET_CH(CH + 1);
                }
                /* eight at a time while more than eight are left or the count is above 255, then
                   the rest (as the original: a 16-bit count under 8 copies 8 and runs on from a
                   wrapped count) */
                do {
                    for (int k = 0; k < 8; k++) movsb_dec();
                } while (8 < CL || CH != 0);
                rep_movsb();
                goto next_code;
            }
        }
        /* 1000:340b: on with the control byte's next bit */
        ctl = shift_control(ctl, false);
        if (R.cf && R.zf) goto reload;
        code = R.cf;
    }
}

/* Unpack_LoadSeries 1000:3547
   Loads the numbered packed files of the name at DS:SI to segment DX on: first 6d78:DI.. is zeroed
   (FFFFh bytes); then for n = 0, 1, ...: the character before the name's terminator becomes '0' + n,
   the file is opened (read whole into 6d78:c000, decoded by Unpack_Decode) and the output, rounded down
   to whole 256-byte pages, copied to DX:0000; DX moves on C00h paragraphs (48 KB) a file. Ends at the
   first name that does not open: CF set, AX = n, BX the DOS error, DX the next segment; DS, ES, SI
   kept; CX and DI as the last copy left them (or as given if no file opened). */
void Unpack_LoadSeries(void)
{
    uint16_t ds = R.ds, es = R.es;
    {
        uint16_t di = R.di, cx = R.cx;
        R.es = SEG_BACK;
        R.ax = 0;
        R.cx = 0xffff;
        rep_stosb(0);
        R.cx = cx, R.di = di;
        R.es = es;
    }
    R.ax = 0;
    for (;;) {
        uint16_t lds = R.ds, lsi = R.si, n = R.ax;
        uint16_t p = R.si;
        while (mb(R.ds, p) != 0) p++;
        p--;
        mbw(R.ds, p, (uint8_t)(n + '0'));
        R.ax = (uint16_t)((n + '0') & 0xff);
        uint16_t seg = R.dx;
        R.dx = R.si;
        R.ax = 0x3dc0;   /* open, read only (deny none, not inherited) */
        mach_int(0x21);
        R.bx = R.ax;
        if (R.cf) {
            R.dx = seg;
            R.ax = n;
            R.si = lsi;
            R.ds = lds;
            R.es = es;
            R.ds = ds;
            return;
        }
        R.dx = 0xc000;
        R.cx = 0xffff;
        R.ax = SEG_BACK;
        R.ds = SEG_BACK;
        SET_AH(0x3f);
        mach_int(0x21);
        SET_AH(0x3e);
        mach_int(0x21);
        Unpack_Decode();
        R.ds = SEG_BACK;
        R.si = 0;
        R.dx = seg;
        R.di = 0;
        R.ax = n;
        R.es = R.dx;
        R.dx = (uint16_t)(R.dx + 0xc00);
        R.cx = R.bx >> 1;
        rep_movsw();
        R.si = lsi;
        R.ds = lds;
        R.ax = (uint16_t)(n + 1);
    }
}
