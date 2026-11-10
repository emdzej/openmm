/* The sound (docs/re/sound.md): the two sound drivers the game loads at 2424:0000, ported routine by
   routine (driver1.c: the AdLib, DRIVER1.BIN; driver2.c: the internal beeper, DRIVER2.BIN), and the two
   devices they drive, modelled here: the AdLib's YM3812 (opl2.c) and the PC speaker on the PIT's channel 2
   (speaker.c). The machine (machine.c) hands the ports 388h/389h and 42h/43h/61h to the models; the
   application mixes them into its audio stream (app.c).

   The drivers are register-exact like the game's routines (docs/re/porting.md): each is `void Name(void)`
   on R, citing `DRIVER1:xxxx` / `DRIVER2:xxxx`, the offset in the file, which is the offset in segment
   2424 where the game loads it. Their names are kept in tools/ghidra/names-driver1.tsv and
   names-driver2.tsv (the two files share their addresses, so they are not in names.tsv). The driver's
   code, tables, music and effects are all in that segment of g_mem, read from the user's file by the
   game: the port reads them there and keeps every variable there, as the original does. */
#pragma once
#include "cpu.h"
#include "mem.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- the driver's segment: DS = CS = 2424 inside every driver routine ---- */
static inline uint8_t o8(uint16_t off) { return mb(SEG_OVL, off); }
static inline uint16_t o16(uint16_t off) { return mw(SEG_OVL, off); }
static inline void o8w(uint16_t off, uint8_t v) { mbw(SEG_OVL, off, v); }
static inline void o16w(uint16_t off, uint16_t v) { mww(SEG_OVL, off, v); }
/* lodsb / lodsw from DS:SI (DF is clear: the entry's cld) */
static inline uint8_t lodsb(void) { uint8_t v = o8(R.si); R.si = (uint16_t)(R.si + 1); return v; }
static inline uint16_t lodsw(void) { uint16_t v = o16(R.si); R.si = (uint16_t)(R.si + 2); return v; }

/* ---- the drivers (drv.c dispatches to them) ---- */
void Driver1_Entry(void);   /* DRIVER1:0000, the far entry: the function in AH */
void Driver2_Entry(void);   /* DRIVER2:0000 */

/* ---- the AdLib: a YM3812 (opl2.c) ---- */
void opl2_reset(void);
void opl2_write_index(uint8_t v);   /* port 388h */
void opl2_write_data(uint8_t v);    /* port 389h */
uint8_t opl2_read_status(void);     /* port 388h read */
/* n samples at rate Hz, mono, added into out (scale: full chip output = +-1.0) */
void opl2_render(float *out, unsigned n, double rate);
/* register write log (tools/run.c --opl-log): called for every data write, index and value */
typedef void (*Opl2Log)(uint8_t index, uint8_t value);
void opl2_set_log(Opl2Log fn);


/* ---- the PC speaker: PIT channel 2 and port 61h (speaker.c) ---- */
void speaker_reset(void);
void speaker_out(uint16_t port, uint8_t v);   /* 42h, 43h (channel 2's mode), 61h */
uint8_t speaker_port61(void);                 /* port 61h as last written */
void speaker_render(float *out, unsigned n, double rate);
