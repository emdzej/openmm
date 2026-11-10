/* The PC underneath the game, modelled as far as the game depends on it (docs/re/architecture.md, "The
   machine"). The ported code keeps the original's ports and interrupt calls: io_in8/io_out8 where it has
   `in`/`out`, mach_int(n) where it has `int n` (with the registers in R, cpu.h), mach_drv() where it
   calls the sound driver's overlay far (2424:0000, the function in AH).

   The frame boundary is the start of the vertical retrace, 70 times a second. The original's own timer
   handler is locked to it (it reprograms the PIT on each tick to fire just before the retrace), so the
   port runs the program's IRQ 0 handler there, then its IRQ 1 handler once per key event, in the order
   the reference harness delivers them; a handler runs only if the program installed it (int 21h 25h)
   and the PIC leaves its line unmasked, else the BIOS's (the day's tick count at 0040:006C). Each wait
   of the original for the retrace or for its timer's counters is a mach_frame().

   A replay mode serves the ports, interrupts and driver calls from a recording of the original instead
   (tools/emu/capture.py), for checking one routine alone (tools/fncheck.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ---- ports ---- */
uint8_t io_in8(uint16_t port);
void io_out8(uint16_t port, uint8_t v);
void io_out16(uint16_t port, uint16_t v);   /* out dx, ax: the index, then the data at port + 1 */

/* ---- interrupts and far calls ---- */
void mach_int(uint8_t n);   /* int n with R as the registers */
void mach_drv(void);        /* call far 2424:0000: the sound driver (or FONT.BIN) loaded there */
/* call far seg:off to code the program found in a vector (pushf first, as for a chained handler): the
   BIOS's handlers at F000:n, or a routine of names.tsv. */
void mach_call_far(uint16_t seg, uint16_t off);

typedef void (*IrqFn)(void);
/* The ported handlers, by the address the original installs (seg:off packed 0xSSSSOOOO). */
void mach_register_handler(uint32_t addr, IrqFn fn);

/* ---- time ---- */
void mach_frame(void);
uint32_t mach_frames(void);

/* ---- the screen ---- */
uint8_t mach_video_mode(void);
const uint8_t *mach_dac(void);   /* 256 x RGB, 6-bit values */

/* ---- machine state ---- */
void mach_reset(void);   /* after mem_load_program: the devices as the BIOS leaves them */

/* ---- replay (the checker) ---- */
typedef struct {
    uint8_t (*in8)(uint16_t port);
    void (*out8)(uint16_t port, uint8_t v);
    void (*intr)(uint8_t n);
    void (*drv)(void);
} MachHooks;
void mach_set_hooks(const MachHooks *h);   /* NULL: the models */
