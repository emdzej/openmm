/* The block copies (docs/re/functions.md). The program patches its own copy loop for 32-bit moves at
   start-up; the port keeps the patched bytes in the code segment and the loop reads them back to choose
   its width, so a patch written anywhere has the original's effect. */
#include "cpu.h"
#include "mem.h"
#include "routines.h"

/* Copy_Patch32 1000:7a75 */
void Copy_Patch32(void)
{
    c16w(0x92d9, 0x40);
    c16w(0x92dc, 0xa566);
    for (uint16_t i = 0; i < 4; i++) c16w((uint16_t)(0x91a4 + 2 * i), 0xa566);   /* four movsd over the movsw */
}
