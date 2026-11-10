/* The PKLITE loader stub of MICRO.EXE, ported: MICRO.EXE is a PKLITE-compressed DOS program, and the
   game's code and tables exist only after the stub at its entry point has run. The port runs the same
   steps on the user's file instead of keeping an unpacked copy (AGENTS.md, hard rules 1 and 2):

   1. stage 1 (image 0x0000, entered at PSP:0100): checks that memory reaches the stub's top, then
      undoes the "extra" protection, a chained xor over the stage-2 code (each word xor its upper
      neighbour, the topmost word xor a seed in the instruction stream), and copies stage 2 up;
   2. stage 2 (image 0x0058 once decrypted): moves the compressed stream up, inflates it to the load
      segment (LZ77: literals xor'ed with the bit counter, lengths and offsets from small tables in the
      stub), applies the relocation list that follows the stream, and enters the program with SS:SP and
      CS:IP from the stream's tail. It also writes "PK" at PSP:5C, which the program may check.

   Addresses in this file are offsets in the packed file's load image (after the 96-byte MZ header) for
   stage 1, and offsets in the decrypted stage-2 code for stage 2. The tables are read from the
   decrypted stub at run time; only the bit layout of the codes is written here. docs/formats/pklite.md
   has the whole format. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *image;          /* the unpacked load image, relocations not applied (segments load-relative) */
    size_t size;
    uint16_t cs, ip, ss, sp; /* segments relative to the load segment */
    uint32_t *reloc;         /* image offsets of the words the loader adds the load segment to */
    int nreloc;
} PkProgram;

/* Unpacks a PKLITE file of the layout MICRO.EXE has (version word 0x310f at file offset 0x1c; the stub
   with the xor-protected second stage). False with err set if the stub is not the one expected or the
   stream is broken. */
bool pklite_unpack(const uint8_t *file, size_t size, PkProgram *out, char *err, size_t errcap);
void pklite_free(PkProgram *p);
