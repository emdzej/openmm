/* The VGA's four planes and the logic in front of them (vga_planar.h). The reference is the harness's
   model (tools/emu/dos.py, class Vga): the same registers, write modes, latches and chain-4 view, so the
   port and the original see the same memory. */
#include "vga_planar.h"
#include "font8x14.h"
#include "mem.h"
#include <string.h>

static uint8_t planes[4][65536];
static struct {
    uint8_t seq_idx, seq[8], gc_idx, gc[16], latch[4];
} v;

static uint8_t *chained(void) { return mem_ptr(SEG_VGA, 0); }

/* A plane byte, and its chain-4 view in g_mem (byte off * 4 + p of A000) where that view reaches. */
static void put(int p, uint16_t off, uint8_t b)
{
    planes[p][off] = b;
    if (off < 0x4000) chained()[off << 2 | p] = b;
}

void vga_planar_reset(void)
{
    memset(planes, 0, sizeof planes);
    memset(&v, 0, sizeof v);
    v.seq[2] = 0x0f, v.seq[4] = 0x06, v.gc[8] = 0xff;
}

bool vga_chain4(void) { return (v.seq[4] & 0x08) != 0; }

/* Chain-4 switched off: the planes take what mode 13h left in A000 (its view covers 4000h bytes each). */
static void unchain(void)
{
    const uint8_t *lin = chained();
    for (uint32_t i = 0; i < 0x10000; i++) planes[i & 3][i >> 2] = lin[i];
}

void vga_planar_mode(uint8_t mode, bool clear)
{
    bool was = vga_chain4();
    if (mode == 0x13) v.seq[2] = 0x0f, v.seq[4] = 0x0e, v.gc[5] = 0x40, v.gc[8] = 0xff;
    else v.seq[2] = 0x0f, v.seq[4] = 0x06, v.gc[5] = 0x00, v.gc[8] = 0xff;
    v.gc[0] = v.gc[1] = v.gc[3] = 0;
    if (clear) memset(planes, 0, sizeof planes);   /* g_mem's A000 is cleared by the caller (bios.c) */
    else if (was && !vga_chain4()) unchain();
}

bool vga_planar_port(uint16_t port) { return port == 0x3c4 || port == 0x3c5 || port == 0x3ce || port == 0x3cf; }

void vga_planar_out(uint16_t port, uint8_t b)
{
    switch (port) {
    case 0x3c4: v.seq_idx = b & 7; break;
    case 0x3c5: {
        bool was = vga_chain4();
        v.seq[v.seq_idx & 7] = b;
        if (was && !vga_chain4()) unchain();
        break;
    }
    case 0x3ce: v.gc_idx = b & 15; break;
    case 0x3cf: v.gc[v.gc_idx & 15] = b; break;
    default: break;
    }
}

uint8_t vga_planar_in(uint16_t port)
{
    switch (port) {
    case 0x3c4: return v.seq_idx;
    case 0x3c5: return v.seq[v.seq_idx & 7];
    case 0x3ce: return v.gc_idx;
    case 0x3cf: return v.gc[v.gc_idx & 15];
    default: return 0xff;
    }
}

uint8_t vga_read8(uint16_t off)
{
    if (vga_chain4()) return chained()[off];
    for (int i = 0; i < 4; i++) v.latch[i] = planes[i][off];
    if (v.gc[5] & 0x08) {   /* read mode 1: a bit set where the pixel's colour matches the compare */
        uint8_t cc = v.gc[2], dc = v.gc[7], r = 0xff;
        for (int i = 0; i < 4; i++)
            if (dc >> i & 1) r &= cc >> i & 1 ? v.latch[i] : (uint8_t)~v.latch[i];
        return r;
    }
    return v.latch[v.gc[4] & 3];
}

void vga_write8(uint16_t off, uint8_t cpu)
{
    if (vga_chain4()) {
        if (v.seq[2] >> (off & 3) & 1) {
            chained()[off] = cpu;
            planes[off & 3][off >> 2] = cpu;
        }
        return;
    }
    int mode = v.gc[5] & 3, rot = v.gc[3] & 7, fn = v.gc[3] >> 3 & 3;
    uint8_t sr = v.gc[0], esr = v.gc[1], mask = v.gc[8];
    if (mode == 1) {   /* the latches as they are */
        for (int i = 0; i < 4; i++)
            if (v.seq[2] >> i & 1) put(i, off, v.latch[i]);
        return;
    }
    if (mode == 0 || mode == 3) cpu = (uint8_t)(cpu >> rot | cpu << (8 - rot));
    if (mode == 3) mask &= cpu;
    for (int i = 0; i < 4; i++) {
        if (!(v.seq[2] >> i & 1)) continue;
        uint8_t b;
        if (mode == 0) b = esr >> i & 1 ? (sr >> i & 1 ? 0xff : 0) : cpu;
        else if (mode == 2) b = cpu >> i & 1 ? 0xff : 0;
        else b = sr >> i & 1 ? 0xff : 0;
        uint8_t l = v.latch[i];
        b = fn == 0 ? b : fn == 1 ? (uint8_t)(b & l) : fn == 2 ? (uint8_t)(b | l) : (uint8_t)(b ^ l);
        put(i, off, (uint8_t)((b & mask) | (l & ~mask)));
    }
}

static bool in_vga(uint16_t seg, uint16_t off, uint16_t *voff)
{
    uint32_t lin = mem_lin(seg, off);
    if (lin < 0xa0000 || lin >= 0xb0000) return false;
    *voff = (uint16_t)(lin - 0xa0000);
    return true;
}

uint8_t vmem_rd8(uint16_t seg, uint16_t off)
{
    uint16_t o;
    if (in_vga(seg, off, &o)) return vga_read8(o);
    return mb(seg, off);
}

void vmem_wr8(uint16_t seg, uint16_t off, uint8_t b)
{
    uint16_t o;
    if (in_vga(seg, off, &o)) vga_write8(o, b);
    else mbw(seg, off, b);
}

/* ---- the BIOS's text in mode 10h: 80 x 25 cells of 8 x 14, pages of 8000h bytes ---- */

void vga_planar_char(uint8_t ch, uint8_t attr, uint8_t page, int row, int col)
{
    if (row < 0 || row > 24 || col < 0 || col > 79) return;
    uint16_t base = (uint16_t)((page & 1) * 0x8000 + row * 14 * 80 + col);
    for (int y = 0; y < 14; y++) {
        uint16_t a = (uint16_t)(base + y * 80);
        uint8_t g = g_font8x14[ch][y];
        for (int p = 0; p < 4; p++) {
            uint8_t on = attr >> p & 1 ? g : 0;
            put(p, a, attr & 0x80 ? (uint8_t)(planes[p][a] ^ on) : on);
        }
    }
}

void vga_planar_scroll(uint8_t page, int top, int bottom)
{
    uint16_t base = (uint16_t)((page & 1) * 0x8000);
    for (int r = top; r <= bottom; r++)
        for (int y = 0; y < 14; y++)
            for (int x = 0; x < 80; x++) {
                uint16_t a = (uint16_t)(base + (r * 14 + y) * 80 + x);
                for (int p = 0; p < 4; p++) put(p, a, r < bottom ? planes[p][(uint16_t)(a + 14 * 80)] : 0);
            }
}

bool vga_planar_render(uint8_t mode, const uint8_t *attr, const uint8_t *crtc, uint8_t *dst, int *w, int *h)
{
    switch (mode) {
    case 0x0d: *w = 320, *h = 200; break;
    case 0x0e: *w = 640, *h = 200; break;
    case 0x10: *w = 640, *h = 350; break;
    case 0x12: *w = 640, *h = 480; break;
    default: return false;
    }
    uint16_t start = (uint16_t)(crtc[0x0c] << 8 | crtc[0x0d]);
    int pitch = crtc[0x13] ? crtc[0x13] * 2 : *w / 8;
    uint8_t amap[16];
    for (int c = 0; c < 16; c++) amap[c] = attr[c] & 0x3f;
    for (int y = 0; y < *h; y++) {
        uint16_t row = (uint16_t)(start + y * pitch);
        for (int xb = 0; xb < *w / 8; xb++) {
            uint16_t a = (uint16_t)(row + xb);
            uint8_t b0 = planes[0][a], b1 = planes[1][a], b2 = planes[2][a], b3 = planes[3][a];
            uint8_t *d = dst + y * *w + xb * 8;
            for (int bit = 0; bit < 8; bit++) {
                int s = 7 - bit;
                d[bit] = amap[(b0 >> s & 1) | (b1 >> s & 1) << 1 | (b2 >> s & 1) << 2 | (b3 >> s & 1) << 3];
            }
        }
    }
    return true;
}

uint8_t *vga_plane(int p) { return planes[p & 3]; }
void vga_planar_poke(int p, uint16_t off, uint8_t b) { put(p & 3, off, b); }

void vga_planar_get(VgaRegs *r)
{
    r->seq_idx = v.seq_idx, r->gc_idx = v.gc_idx;
    memcpy(r->seq, v.seq, 8), memcpy(r->gc, v.gc, 16), memcpy(r->latch, v.latch, 4);
}

void vga_planar_set(const VgaRegs *r)
{
    v.seq_idx = r->seq_idx, v.gc_idx = r->gc_idx;
    memcpy(v.seq, r->seq, 8), memcpy(v.gc, r->gc, 16), memcpy(v.latch, r->latch, 4);
}
