/* The VGA's memory as four planes (docs/re/protection.md, "The machine"): the 16-colour modes 0Dh, 0Eh,
   10h and 12h, which the code card (FONT.BIN, mode 10h) and SM.EXE's exit (mode 0Eh) use. The game itself
   runs in mode 13h, where A000 is linear memory in g_mem and this model stays out of the way.

   Behind A000 in a planar mode are four 64 KB planes, written through the sequencer's map mask and the
   graphics controller (set/reset, rotate and function, write modes 0-3, the bit mask) and read through
   the latches (read mode 0: one plane; 1: colour compare), as the reference harness models them
   (tools/emu/dos.py, class Vga). Chain-4 (sequencer register 4, bit 3: mode 13h) maps A000 byte i to plane
   i & 3 at i >> 2; the model keeps that view of the planes in g_mem at A000 at all times (the first 4000h
   bytes of each plane), so mode 13h code reads and writes g_mem as before and the checker compares A000
   as the harness snapshots it. Ported code that touches A000 in a planar mode goes through vga_read8 /
   vga_write8 (or vmem_rd8 / vmem_wr8, which route by segment). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void vga_planar_reset(void);   /* power-on: the planes cleared, the registers as the harness starts */
/* int 10h's mode set: the registers' defaults for the mode (as the harness sets them), the planes
   cleared unless clear is false (AL bit 7). */
void vga_planar_mode(uint8_t mode, bool clear);
bool vga_chain4(void);

/* The sequencer (3C4h/3C5h) and the graphics controller (3CEh/3CFh). */
bool vga_planar_port(uint16_t port);
void vga_planar_out(uint16_t port, uint8_t v);
uint8_t vga_planar_in(uint16_t port);

/* A CPU byte access to A000:off in a planar mode (latches loaded on a read). */
uint8_t vga_read8(uint16_t off);
void vga_write8(uint16_t off, uint8_t v);
/* Any seg:off: A000-AFFFF through the planes when chain-4 is off, else g_mem. */
uint8_t vmem_rd8(uint16_t seg, uint16_t off);
void vmem_wr8(uint16_t seg, uint16_t off, uint8_t v);

/* The BIOS's character cell: 8 x 14 pixels of g_font8x14 at text row, column of page (mode 10h), colour
   attr's low four bits; attr bit 7 XORs the glyph in, else the cell is written (background 0). */
void vga_planar_char(uint8_t ch, uint8_t attr, uint8_t page, int row, int col);
/* Scrolls rows top..bottom of a 350-line page up by one text row, the freed row in colour 0. */
void vga_planar_scroll(uint8_t page, int top, int bottom);

/* The screen of a planar mode as 8-bit indices into the DAC (through the attribute palette, attr[0..15]
   & 3Fh), from the CRTC's start address (crtc[0Ch], [0Dh]) at crtc[13h] * 2 bytes a line. False for the
   other modes. dst holds 640 x 480. */
bool vga_planar_render(uint8_t mode, const uint8_t *attr, const uint8_t *crtc, uint8_t *dst, int *w, int *h);

/* For the checker (tools/fncheck.c): the planes and the registers as the harness records them. */
uint8_t *vga_plane(int p);   /* 65536 bytes */
void vga_planar_poke(int p, uint16_t off, uint8_t v);   /* a plane byte and its chained view in g_mem */
typedef struct { uint8_t seq_idx, seq[8], gc_idx, gc[16], latch[4]; } VgaRegs;
void vga_planar_get(VgaRegs *r);
void vga_planar_set(const VgaRegs *r);
