/* The block copies (docs/re/functions.md). The program patches its own copy loops for 32-bit moves at
   start-up; the port keeps the patched bytes in the code segment and runs the loops from those bytes
   (copy_run_code), so a patch written anywhere has the original's effect. */
#include "copy.h"
#include "cpu.h"
#include "gfx.h"
#include "mem.h"
#include "platform.h"
#include "routines.h"

/* Copy_Patch32 1000:7a75 */
void Copy_Patch32(void)
{
    c16w(0x92d9, 0x40);
    c16w(0x92dc, 0xa566);
    for (uint16_t i = 0; i < 4; i++) c16w((uint16_t)(0x91a4 + 2 * i), 0xa566);   /* four movsd over the movsw */
}

void copy_run_code(uint16_t from, uint16_t to)
{
    uint16_t ip = from;
    while (ip < to) {
        bool rep = false, op32 = false;
        uint8_t b = c8(ip++);
        for (;; b = c8(ip++)) {   /* prefixes */
            if (b == 0xf3) rep = true;
            else if (b == 0x66) op32 = true;
            else break;
        }
        switch (b) {
        case 0x90: break;                                 /* nop */
        case 0x87:                                        /* xchg bx, bx (87 db), a filler */
            ip++;
            break;
        case 0xb9:                                        /* mov cx, imm16 */
            R.cx = c16(ip);
            ip = (uint16_t)(ip + 2);
            break;
        case 0xa5:                                        /* movsw / movsd */
            if (rep) {
                for (; R.cx; R.cx--) op32 ? movsd() : movsw();
            } else {
                op32 ? movsd() : movsw();
            }
            break;
        default: {
            /* not one of the two loops' forms: the code was patched in a way the port does not know */
            static int seen;
            if (!seen) {
                seen = 1;
                plat_log("copy_run_code: unknown instruction in a copy loop");
            }
            return;
        }
        }
    }
}
