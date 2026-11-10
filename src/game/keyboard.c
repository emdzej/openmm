/* The keyboard: the program's IRQ 1 handler (docs/re/functions.md, "keyboard"). It keeps, in the data
   segment, a bit mask of the sixteen keys the set-up assigns (their scan codes at 193c:106c, from
   SETTINGS.DAT), the last key pressed and the last released, and Esc's release as a request to quit. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* Key_Irq 1000:2efd
   cs:2f99   set by an E0 prefix; the fake shift an extended key sends (2a/36 after E0) clears it and is
             ignored. Any other key leaves it set (as the original), so after an arrow key the next real
             Shift press is taken for a fake one and dropped.
   193c:107c the assigned keys held, bit 15 for the first of the table at 193c:106c
   193c:107e the last key released (scan code without bit 7), 193c:107f the last pressed (0 when taken)
   193c:1096 1 once Esc is released */
void Key_Irq(void)
{
    uint8_t code = io_in8(0x60);
    uint8_t ack = io_in8(0x61);
    io_out8(0x61, ack | 0x80);    /* acknowledge to the keyboard controller */
    io_out8(0x61, ack & 0x7f);
    uint8_t key = code & 0x7f;
    if (code == 0xe0) {
        c8w(0x2f99, 1);
    } else if ((code == 0x2a || code == 0x36) && c8(0x2f99) == 1) {
        c8w(0x2f99, 0);
    } else {
        bool release = code & 0x80;
        if (release) {
            if (d8(0x107e) != 0 || d8(0x107f) == key) {
                d8w(0x107f, 0);
                d8w(0x107e, key);
                if (key == 1) d8w(0x1096, 1);   /* Esc */
            }
        } else if (d8(0x107f) == 0) {
            d8w(0x107f, key);
        }
        uint16_t bit = 0x8000;
        for (uint16_t di = 0x106c; bit; di++, bit >>= 1) {
            if (d8(di) != key) continue;
            if (release) d16w(0x107c, d16(0x107c) & (uint16_t)~bit);
            else d16w(0x107c, d16(0x107c) | bit);
            break;
        }
    }
    io_out8(0x20, 0x61);   /* specific end of interrupt, IRQ 1 */
}
