# PKLITE (`MICRO.EXE`)

`MICRO.EXE` is compressed with PKLITE (the banner "PKLITE Copr. 1990-92 PKWARE Inc." at file offset 0x1e,
the version word 0x310f at 0x1c). The port does what the stub at the program's entry does
(`src/pklite.c`), reading the stub's own tables and operands from the user's file. Offsets below: stage
1 in the file's load image (after the 96-byte MZ header; it runs at PSP:0100), stage 2 in its decrypted
code.

## Stage 1 (image 0x000-0x057)

1. `AX` = the stub's top (7DA9h paragraphs, patched with the load segment by the file's only
   relocation); if it is not below the memory top in the PSP (PSP:0002), print "Not enough memory" and
   exit.
2. `SS` = top - 20h; the destination of stage 2 is top - 46h.
3. Decrypt (the "extra" protection): `CX - 1` = 0x136 words, from image 0x2b2 down to 0x048, direction
   flag set. Each word becomes itself xor the word above it as stored, the topmost xor the seed 0x070c
   (`mov dx, 070ch`). The seed is also the length of the compressed stream in paragraphs, kept on the
   stack for stage 2.
4. Copy stage 2 (0x12e words from image 0x058) to the destination and `retf` to it.

## Stage 2

1. Move the compressed stream (from PSP + 3Ch paragraphs, i.e. image 0x2c0, 0x70c paragraphs) up, under
   stage 2, in 64 KB steps backwards, so the output can grow over the old file.
2. Inflate from there to PSP:0100 (the load segment):
   - Bits come from 16-bit words of the stream, lowest first; a new word is fetched as the 16th bit is
     taken (the bit read is the one shifted out before the reload). Bytes and words share one stream.
   - Bit 0: a literal byte, xor the number of bits now left in the word (1-16).
   - Bit 1: a copy. Its length: `1x` gives 2 + x; then 3, 4 and 5-bit codes index the first length table
     (12 entries at stage 2 + 0x21d), a second group the second table (+0x228), a third group the third
     (+0x233). An entry of 0x19 in the second table means 0x19 + the next byte; that byte 0xfe means
     "renormalise the segments" (no copy) and 0xff ends the data.
   - The distance's low byte is the next byte. Its high byte is 0 for a copy of 2, or after a 1 bit;
     otherwise a 3, 4 or 5-bit code indexes the distance table (+0x243), and a 6-bit code is the value
     itself with bit 5 cleared.
   - Copies run forward byte by byte, so they can overlap their source.
3. The relocations follow the data: in groups for each 0FFFh paragraphs of the image, a count then that
   many offsets (count 0: an empty group; 0FFFFh: the end). Each word gets the load segment added.
4. Then SS, SP, CS and IP relative to the load segment. The stub writes "PK" at PSP:005C (a mark the
   program can test), clears the registers and `retf`s to the program.

For MICRO.EXE: 82,493 bytes out, 279 relocations (all in the first group), SS:SP 7d78:0400 and CS:IP
0000:0006 relative to the load segment (8d78:0400 and 1000:0006 in the port's numbering).

The table offsets in the list above are where the instructions that index them point
(`mov bl, cs:[bx+...]`); the port reads them from those instructions.
