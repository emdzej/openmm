/* The real-mode memory the original program works in (docs/re/architecture.md, "Memory"). The game is
   hand-written assembly that addresses everything as segment:offset: its variables at offsets of the
   data segment, its buffers at fixed segments, even variables in its own code segment. The port keeps
   that memory, 1 MB as DOS addresses it, with the unpacked program loaded and relocated at 1000:0000,
   the PSP below it and the BIOS data area at 0040:0000, so that every ported function reads and writes
   what the original did, where the original did; overlaps and out-of-bounds reads behave the same.

   Offsets wrap at 64 KB within a segment, as on the CPU. Words are little-endian and may be unaligned. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SEG_ENV = 0x0f00,   /* the environment block */
    SEG_PSP = 0x0ff0,   /* the program segment prefix */
    SEG_CODE = 0x1000,  /* the program's code (and its cs: variables) */
    SEG_DATA = 0x193c,  /* the data segment: DS from Start on */
    SEG_OVL = 0x2424,   /* the overlay: FONT.BIN (the code card), then the sound driver DRIVERn.BIN */
    SEG_STACK = 0x8d78, /* the stack after Start */
    SEG_VGA = 0xa000,   /* VGA memory (mode 13h: the screen, linear) */
    SEG_BIOS = 0x0040,  /* the BIOS data area */
};

#define MEM_SIZE 0x110000u   /* 1 MB plus the 64 KB above it that FFFF:xxxx reaches */

extern uint8_t g_mem[MEM_SIZE];

static inline uint32_t mem_lin(uint16_t seg, uint16_t off) { return ((uint32_t)seg << 4) + off; }
static inline uint8_t *mem_ptr(uint16_t seg, uint16_t off) { return g_mem + mem_lin(seg, off); }

static inline uint8_t mb(uint16_t seg, uint16_t off) { return g_mem[mem_lin(seg, off)]; }
static inline uint16_t mw(uint16_t seg, uint16_t off)
{
    return (uint16_t)(g_mem[mem_lin(seg, off)] | g_mem[mem_lin(seg, (uint16_t)(off + 1))] << 8);
}
static inline void mbw(uint16_t seg, uint16_t off, uint8_t v) { g_mem[mem_lin(seg, off)] = v; }
static inline void mww(uint16_t seg, uint16_t off, uint16_t v)
{
    g_mem[mem_lin(seg, off)] = (uint8_t)v;
    g_mem[mem_lin(seg, (uint16_t)(off + 1))] = (uint8_t)(v >> 8);
}
static inline uint32_t md(uint16_t seg, uint16_t off) { return mw(seg, off) | (uint32_t)mw(seg, (uint16_t)(off + 2)) << 16; }
static inline void mdw(uint16_t seg, uint16_t off, uint32_t v)
{
    mww(seg, off, (uint16_t)v);
    mww(seg, (uint16_t)(off + 2), (uint16_t)(v >> 16));
}

/* The data segment's variables: D8(0x107e) is byte [107e] of the original (DS = 193c). */
static inline uint8_t d8(uint16_t off) { return mb(SEG_DATA, off); }
static inline uint16_t d16(uint16_t off) { return mw(SEG_DATA, off); }
static inline void d8w(uint16_t off, uint8_t v) { mbw(SEG_DATA, off, v); }
static inline void d16w(uint16_t off, uint16_t v) { mww(SEG_DATA, off, v); }
/* The code segment's own variables (cs:[4a92] and the like). */
static inline uint8_t c8(uint16_t off) { return mb(SEG_CODE, off); }
static inline uint16_t c16(uint16_t off) { return mw(SEG_CODE, off); }
static inline void c8w(uint16_t off, uint8_t v) { mbw(SEG_CODE, off, v); }
static inline void c16w(uint16_t off, uint16_t v) { mww(SEG_CODE, off, v); }

/* Fills the memory as DOS leaves it for MICRO.EXE: the unpacked image (exe.h) at 1000:0000 with its
   relocations applied, the PSP (memory top A000h, the environment, the "PK" PKLITE's stub writes at
   PSP:005C), the environment, the BIOS data area (mode 3, the CRTC at 3D4h, the tick count). False if
   the exe is not loaded. */
bool mem_load_program(void);
/* A file's bytes into memory at seg:off (the overlays, the data files read whole). */
void mem_write(uint16_t seg, uint16_t off, const void *src, size_t len);
void mem_read(uint16_t seg, uint16_t off, void *dst, size_t len);
