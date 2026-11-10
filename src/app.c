/* The application (app.h): the exe found, checked and unpacked, then MICRO.COM's order run
   (launcher.c): SM.EXE's splash, then MICRO.EXE from its entry, Start (1000:0006). */
#include "app.h"
#include "cpu.h"
#include "exe.h"
#include "machine.h"
#include "mem.h"
#include "platform.h"
#include "launcher.h"
#include "routines.h"
#include "sound/sound.h"
#include <stdio.h>
#include <string.h>

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

int app_main(void)
{
    int code = Launcher_Main();
    plat_exit(code);
    return code;
}

/* A frame of sound: the AdLib and the speaker as the drivers left them during the frame (sound.h). */
void app_audio(float *stereo, unsigned frames)
{
    static float mono[8192];
    if (frames > 8192) frames = 8192;
    memset(mono, 0, frames * sizeof *mono);
    opl2_render(mono, frames, APP_AUDIO_RATE);
    speaker_render(mono, frames, APP_AUDIO_RATE);
    for (unsigned i = 0; i < frames; i++) {
        float v = mono[i] > 1.0f ? 1.0f : mono[i] < -1.0f ? -1.0f : mono[i];
        stereo[2 * i] = stereo[2 * i + 1] = v;
    }
}

void app_exit(void) { exe_free(); }
