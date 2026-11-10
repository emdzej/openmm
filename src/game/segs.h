/* The far buffers at fixed segments, as the program uses them after DOS relocates it (the file's
   operands are 1000h lower: 6d78 in the file is 7d78 here). From the relocation table: each is the
   segment a relocated operand in the code names (docs/re/race-setup.md has the layouts). */
#pragma once
enum {
    SEG_TILES = 0x2b78,   /* the decoder's big output: the front end's pictures, BITSFILE, the track's tiles (16 x 16 at 2b78 + 10h n) */
    SEG_MAP = 0x4b78,     /* the track map, 192 x 192 words (rows 96.. at 5478) */
    SEG_MAP2 = 0x5478,
    SEG_CARS = 0x5d78,    /* the vehicles' pictures: 32 directions */
    SEG_EFFECTS = 0x6d78, /* the rest of the vehicle file: the other frames (shadows, effects) */
    SEG_BACK = 0x7d78,    /* the work segment: the back buffer, the decoder, the fades' palettes */
};
