/* The patched copy loops (copy.c, docs/re/drawing.md). */
#pragma once
#include <stdint.h>

/* Runs the copy instructions at cs:from up to cs:to on R as the CPU would, reading the bytes back from
   the code segment, so that Copy_Patch32's rewrite has its effect whenever it was made. Known forms:
   mov cx, imm16 (b9), rep (f3), operand size (66), movsw / movsd (a5), nop (90), xchg bx, bx (87 db).
   The two places: 1000:91a4..91ab, eight movsw (patched: four movsd), one 16-byte tile row of
   Race_DrawFrame (copy_run_code(0x91a4, 0x91ac)); 1000:92d8..92dd, mov cx, 80h; rep movsw; nop
   (patched: mov cx, 40h; rep movsd), one screen row of Gfx_PresentScrolled. */
void copy_run_code(uint16_t from, uint16_t to);
