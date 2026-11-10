/* The far calls into the overlay at 2424:0000 (machine.h, mach_drv): whichever file the program loaded
   there. At start-up FONT.BIN, the code-card check (AX = 0 passes); then the sound driver DRIVERn.BIN,
   a function number in AH. DRIVER0.BIN, "no sound", returns AX = 0 for every function; the AdLib and
   beeper drivers are ported with the sound (port-plan step 7). Recognised by their first bytes. */
#include "cpu.h"
#include "mem.h"
#include "pending.h"

void drv_call(void)
{
    const uint8_t *p = mem_ptr(SEG_OVL, 0);
    if (p[0] == 0xb8 && p[1] == 0 && p[2] == 0 && p[3] == 0xcb) {   /* DRIVER0: mov ax, 0; retf */
        R.ax = 0;
        return;
    }
    if (p[0] == 0x50 && p[1] == 0x53 && p[2] == 0x51 && p[3] == 0x52) {   /* FONT.BIN */
        PENDING_NOTE(0x24240000, "CodeCard_Check (FONT.BIN): passed without the card");
        R.ax = 0;
        return;
    }
    PENDING_NOTE(0x24240001, "SoundDriver (DRIVER1/2.BIN)");
    R.ax = 0;
}
