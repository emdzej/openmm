/* The PKLITE stub of MICRO.EXE, ported (see pklite.h). */
#include "pklite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* An instruction's 16-bit operand at code[at], after checking the opcode bytes before it (op, n bytes):
   the stub is read, not assumed. False if they differ. */
static bool operand(const uint8_t *code, size_t len, size_t at, const char *op, size_t n, uint16_t *v)
{
    if (at < n || at + 2 > len || memcmp(code + at - n, op, n) != 0) return false;
    *v = rd16(code + at);
    return true;
}

/* ---- stage 2's input: one stream of bytes, with 16-bit words of bits taken from it as they run out ---- */

typedef struct {
    const uint8_t *p, *end;
    uint16_t bits;  /* BP: the bits not yet used, next one lowest */
    int count;      /* DX: bits left in BP, 1-16 */
    bool bad;       /* read past the end */
} Stream;

static uint8_t in_byte(Stream *s)
{
    if (s->p >= s->end) { s->bad = true; return 0; }
    return *s->p++;
}

static uint16_t in_word(Stream *s)
{
    uint8_t lo = in_byte(s);
    return (uint16_t)(lo | in_byte(s) << 8);
}

/* stub 0x088 and its copies: shr bp,1 / dec dx / reload when the count reaches 0. The bit is the one
   shifted out before the reload (lodsw and mov leave CF alone). */
static int in_bit(Stream *s)
{
    int b = s->bits & 1;
    s->bits >>= 1;
    if (--s->count == 0) {
        s->bits = in_word(s);
        s->count = 16;
    }
    return b;
}

static int more(Stream *s, int v) { return v << 1 | in_bit(s); }   /* rcl bx,1 after a bit */

/* The tables of stage 2, by the displacements of the instructions that index them. */
typedef struct { const uint8_t *len_a, *len_b, *len_c, *dist_hi; } Tables;

/* Copy length, stub 0x08f-0x1d1. 0 for the escape codes (*esc: 0xfe = renormalise the segments, 0xff =
   the end of the stream). */
static unsigned copy_length(Stream *s, const Tables *t, int *esc)
{
    *esc = 0;
    int v = more(s, in_bit(s));                 /* 2 bits */
    if (v >= 2) return (unsigned)v;             /* 1x: 2 or 3 */
    v = more(s, v);                             /* 3 bits */
    if (v == 0) return t->len_a[0];
    v = more(s, v);                             /* 4 bits: 2-7 */
    if (v < 5) return t->len_a[v];
    v = more(s, v);                             /* 5 bits: 10-15 */
    if (v <= 12) return t->len_a[v];
    v = more(s, v & 3);                         /* stub 0x146: the second group, 2-7 */
    if (v >= 5) {
        v = more(s, v);                         /* 10-15 */
        if (v > 12) {                           /* stub 0x1ae: the third group */
            v = more(s, v & 3);
            if (v >= 5) v = more(s, v);
            return t->len_c[v];
        }
    }
    unsigned n = t->len_b[v];
    if (n != 0x19) return n;
    uint8_t x = in_byte(s);                     /* stub 0x176: 0x19 + a byte, 0xfe and 0xff reserved */
    if (x >= 0xfe) { *esc = x; return 0; }
    return 0x19u + x;
}

/* High byte of the copy distance, stub 0x0d0-0x144 (the low byte follows in the stream). */
static unsigned distance_high(Stream *s, const Tables *t, unsigned len)
{
    if (len == 2 || in_bit(s)) return 0;
    int v = more(s, more(s, in_bit(s)));        /* 3 bits */
    if (v < 2) return t->dist_hi[v];
    v = more(s, v);                             /* 4 bits */
    if (v < 8) return t->dist_hi[v];
    v = more(s, v);                             /* 5 bits */
    if (v < 0x17) return t->dist_hi[v];
    v = more(s, v);                             /* 6 bits, bit 5 dropped (and bx,0dfh) */
    return (unsigned)(v & 0xdf);
}

void pklite_free(PkProgram *p)
{
    free(p->image);
    free(p->reloc);
    memset(p, 0, sizeof *p);
}

static bool fail(char *err, size_t cap, const char *why)
{
    if (err && cap) snprintf(err, cap, "PKLITE: %s", why);
    return false;
}

bool pklite_unpack(const uint8_t *file, size_t size, PkProgram *out, char *err, size_t errcap)
{
    memset(out, 0, sizeof *out);
    if (size < 0x40 || file[0] != 'M' || file[1] != 'Z') return fail(err, errcap, "not an MZ program");
    size_t hdr = (size_t)rd16(file + 8) * 16;
    if (hdr >= size) return fail(err, errcap, "bad header");
    size_t n = size - hdr;
    uint8_t *img = malloc(n);
    if (!img) return fail(err, errcap, "out of memory");
    memcpy(img, file + hdr, n);

    /* Stage 1. Its code runs at PSP:0100, so its offsets are the image's plus 0x100. */
    uint16_t seed, words, top;
    bool ok = operand(img, n, 0x04, "\xba", 1, &seed)            /* mov dx, seed (also the stream's paragraphs) */
              && operand(img, n, 0x35, "\xb9", 1, &words)        /* mov cx, words + 1 */
              && operand(img, n, 0x38, "\xbe", 1, &top)          /* mov si, the topmost word */
              && top >= 0x100 + 2u * (words - 1u) && (size_t)top - 0x100 + 2 <= n;
    if (!ok) { free(img); return fail(err, errcap, "unexpected stage 1"); }
    uint16_t prev = seed;
    for (unsigned i = 1, at = top - 0x100u; i < words; i++, at -= 2) {   /* std; lodsw; xchg dx,ax; xor ax,dx; stosw */
        uint16_t w = rd16(img + at);
        uint16_t v = prev ^ w;
        img[at] = (uint8_t)v, img[at + 1] = (uint8_t)(v >> 8);
        prev = w;
    }
    uint16_t s2words, s2src;
    ok = operand(img, n, 0x4c, "\xb9", 1, &s2words)                /* mov cx, stage 2's words */
         && operand(img, n, 0x52, "\xbe", 1, &s2src)                /* mov si, stage 2 */
         && s2src >= 0x100 && (size_t)s2src - 0x100 + 2u * s2words <= n;
    if (!ok) { free(img); return fail(err, errcap, "unexpected stage 1 after decryption"); }
    const uint8_t *s2 = img + (s2src - 0x100);
    size_t s2len = 2u * s2words;

    /* Stage 2: where the stream starts (add bx, paragraphs from the PSP) and the four tables. */
    uint16_t data_par, ta, tb, tc, td;
    ok = operand(s2, s2len, 0x06, "\x81\xc3", 2, &data_par)
         && operand(s2, s2len, 0xcc, "\x2e\x8a\x9f", 3, &ta)       /* mov bl, cs:[bx+len_a] */
         && operand(s2, s2len, 0x16c, "\x2e\x8a\x8f", 3, &tb)      /* mov cl, cs:[bx+len_b] */
         && operand(s2, s2len, 0x1cf, "\x2e\x8a\x8f", 3, &tc)      /* mov cl, cs:[bx+len_c] */
         && operand(s2, s2len, 0xff, "\x2e\x8a\xbf", 3, &td)       /* mov bh, cs:[bx+dist_hi] */
         && data_par >= 0x10 && ta + 13u <= s2len && tb + 13u <= s2len && tc + 16u <= s2len && td + 23u <= s2len;
    if (!ok) { free(img); return fail(err, errcap, "unexpected stage 2"); }
    Tables t = { s2 + ta, s2 + tb, s2 + tc, s2 + td };
    size_t data = (size_t)(data_par - 0x10) * 16, data_end = data + (size_t)seed * 16;
    if (data >= n) { free(img); return fail(err, errcap, "no stream"); }
    if (data_end > n) data_end = n;    /* the copy moves whole paragraphs; the file ends inside the last */

    Stream s = { img + data, img + data_end, 0, 16, false };
    s.bits = in_word(&s);              /* lodsw; xchg bp,ax; mov dx,10h */
    size_t cap = 1u << 16, len = 0;
    uint8_t *o = malloc(cap);
    if (!o) { free(img); return fail(err, errcap, "out of memory"); }
    const size_t limit = 640u << 10;   /* conventional memory: the stub sits at its top */
    for (;;) {
        if (s.bad) break;
        if (len + 0x200 > cap) {
            if (cap >= limit) { s.bad = true; break; }
            uint8_t *g = realloc(o, cap * 2);
            if (!g) { s.bad = true; break; }
            o = g, cap *= 2;
        }
        if (!in_bit(&s)) {                     /* stub 0x084: a literal, xor'ed with the bits left */
            uint8_t b = in_byte(&s);
            o[len++] = (uint8_t)(b ^ s.count);
            continue;
        }
        int esc;
        unsigned cnt = copy_length(&s, &t, &esc);
        if (esc == 0xff) break;                /* stub 0x1d4: the end */
        if (esc == 0xfe) continue;             /* stub 0x183: ES:DI and DS:SI renormalised; flat here */
        unsigned hi = distance_high(&s, &t, cnt);
        size_t dist = (size_t)hi << 8 | in_byte(&s);
        if (dist == 0 || dist > len) { s.bad = true; break; }
        for (unsigned i = 0; i < cnt; i++, len++) o[len] = o[len - dist];   /* rep movsb, overlapping */
    }
    /* stub 0x1d4-0x21c: the relocations in groups of 0xfff paragraphs (a count, then that many offsets;
       0xffff ends the list), then SS, SP, CS, IP relative to the load segment. */
    uint32_t *rel = NULL;
    int nrel = 0, relcap = 0;
    for (uint32_t group = 0; !s.bad; group += 0xfff) {
        uint16_t c = in_word(&s);
        if (c == 0xffff) break;
        for (unsigned i = 0; i < c && !s.bad; i++) {
            uint32_t at = group * 16 + in_word(&s);
            if (nrel == relcap) {
                int nc = relcap ? relcap * 2 : 256;
                uint32_t *g = realloc(rel, (size_t)nc * sizeof *g);
                if (!g) { s.bad = true; break; }
                rel = g, relcap = nc;
            }
            rel[nrel++] = at;
        }
    }
    out->ss = in_word(&s);
    out->sp = in_word(&s);
    out->cs = in_word(&s);
    out->ip = in_word(&s);
    free(img);
    for (int i = 0; !s.bad && i < nrel; i++)
        if (rel[i] + 2 > len) s.bad = true;
    if (s.bad) {
        free(o);
        free(rel);
        memset(out, 0, sizeof *out);
        return fail(err, errcap, "broken stream");
    }
    out->image = o;
    out->size = len;
    out->reloc = rel;
    out->nreloc = nrel;
    return true;
}
