/* The application (see app.h). Step 0 of docs/re/port-plan.md: the exe is found, checked and unpacked,
   and a blank 320 x 200 frame is shown. MICRO.COM's order (SM.EXE, then MICRO.EXE) and the program's
   own start-up come next. */
#include "app.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

enum { SCREEN_W = 320, SCREEN_H = 200 };

static uint8_t screen[SCREEN_W * SCREEN_H];   /* VGA memory at A000:0000 in mode 13h */
static uint8_t dac[768];                     /* the palette, widened to 8 bits */

bool app_init(void)
{
    char err[256], m[320];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenMM: %s", err);
        plat_log(m);
        return false;
    }
    const ExeEntry *e = exe_entry();
    snprintf(m, sizeof m, "OpenMM: MICRO.EXE (1994 floppy release) found; unpacked %u bytes, entry %04x:%04x, "
             "stack %04x:%04x, %d relocations", EXE_IMAGE_SIZE, e->cs, e->ip, e->ss, e->sp, e->nreloc);
    plat_log(m);
    return true;
}

bool app_frame(void)
{
    PENDING_NOTE(0x10000006, "Start");   /* the program's entry, 1000:0006 */
    plat_present(screen, SCREEN_W, SCREEN_H, SCREEN_W, dac);
    return true;
}

void app_audio(float *stereo, unsigned frames) { memset(stereo, 0, (size_t)frames * 2 * sizeof *stereo); }

void app_exit(void) { exe_free(); }
