/* The far calls into the overlay at 2424:0000 (machine.h, mach_drv): whichever file the program loaded
   there. At start-up FONT.BIN, the code-card check (AX = 0 passes); then the sound driver DRIVERn.BIN,
   a function number in AH. DRIVER0.BIN, "no sound", returns AX = 0 for every function; the AdLib and
   beeper drivers are ported with the sound (port-plan step 7). Recognised by their first bytes. */
#include "cpu.h"
#include "exe.h"
#include "mem.h"
#include "pending.h"
#include "sound/sound.h"

/* The sound drivers (src/sound/, docs/re/sound.md): DRIVER1.BIN (the AdLib) and DRIVER2.BIN (the beeper)
   are recognised by their far entry (call 0094 / call 00F7, retf) and a CRC-32 of their code as loaded
   (the bytes no variable shares: 0094-09D2 less 00C3 in DRIVER1, 00F7-0688 less 0124 in DRIVER2), taken
   at every call (2.3 KB at most; the game reloads a driver over FONT.BIN and over itself). */
#define DRIVER1_CODE_CRC 0x33ccf130u
#define DRIVER2_CODE_CRC 0x0a6c1bb3u

static uint32_t code_crc(uint16_t from, uint16_t var, uint16_t to)
{
    static uint8_t buf[0x1000];
    size_t n = 0;
    for (uint16_t o = from; o < to; o++)
        if (o != var) buf[n++] = mb(SEG_OVL, o);
    return crc32(buf, n);
}

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
    /* call 0094 / call 00f7, retf: the two drivers' far entries */
    if (p[0] == 0xe8 && p[1] == 0x91 && p[2] == 0x00 && p[3] == 0xcb && code_crc(0x0094, 0x00c3, 0x09d3) == DRIVER1_CODE_CRC) {
        Driver1_Entry();
        return;
    }
    if (p[0] == 0xe8 && p[1] == 0xf4 && p[2] == 0x00 && p[3] == 0xcb && code_crc(0x00f7, 0x0124, 0x0689) == DRIVER2_CODE_CRC) {
        Driver2_Entry();
        return;
    }
    PENDING_NOTE(0x24240001, "an unknown program at 2424:0000 (not DRIVER0/1/2.BIN of the 1994 release)");
    R.ax = 0;
}
