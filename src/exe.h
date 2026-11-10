/* The original game program, read at run time: the code's tables, strings and constants are taken from
   the user's MICRO.EXE rather than copied into the source tree (AGENTS.md, hard rule 2). Only the
   supported build is accepted: MICRO.EXE of the 720 KB floppy release (dated 1994-09-14), 28,955 bytes,
   CRC-32 8e84eb22, a PKLITE-compressed DOS program. Its stub is run by the port (pklite.h) and the
   unpacked image checked too: 82,493 bytes, CRC-32 943cfcf8, 279 relocations, entry 1000:0006.

   Addresses are real-mode seg:off with the program loaded at segment 1000h (EXE_LOAD_SEG), as Ghidra's
   MZ loader and tools/emu place it, so every citation reads the same in the port, in Ghidra and in the
   emulator: the entry is 1000:0006, the code segment 1000, the second (strings, tables) 193c. EXE_LIN
   turns one into an image offset. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXE_PATH "MICRO.EXE"
#define EXE_SIZE 28955u
#define EXE_CRC32 0x8e84eb22u
#define EXE_IMAGE_SIZE 82493u
#define EXE_IMAGE_CRC32 0x943cfcf8u
#define EXE_NRELOC 279

#define EXE_LOAD_SEG 0x1000u
#define EXE_LIN(seg, off) (((uint32_t)(seg) - EXE_LOAD_SEG) * 16u + (uint32_t)(off))

/* Loads, checks and unpacks the exe through the file layer. False (err says why) if missing, another
   build, or the unpacked image is not the one expected. */
bool exe_init(char *err, size_t errcap);
bool exe_loaded(void);
void exe_free(void);
/* The unpacked load image (relocations not applied) and its size. */
const uint8_t *exe_image(size_t *size);
/* len bytes at image offset lin, or NULL if not inside the image. */
const uint8_t *exe_data(uint32_t lin, size_t len);
/* Little-endian reads at an image offset; 0 if outside. */
uint8_t exe_u8(uint32_t lin);
uint16_t exe_u16(uint32_t lin);
uint32_t exe_u32(uint32_t lin);
/* The NUL-terminated string at lin, or "" if unavailable. DOS strings ending in '$' are the caller's. */
const char *exe_str(uint32_t lin);
/* The program's entry state (segments with EXE_LOAD_SEG added, as the loader leaves them) and its
   relocations (image offsets of the words that get EXE_LOAD_SEG added). */
typedef struct { uint16_t cs, ip, ss, sp; int nreloc; const uint32_t *reloc; } ExeEntry;
const ExeEntry *exe_entry(void);

uint32_t crc32(const void *data, size_t len);
