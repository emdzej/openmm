/* The application. The original is a DOS program with its own loops, timed by its IRQ 0 handler; the
   port keeps them (gasm LOOP): app_main runs the program from its entry, and every wait of the original
   for its timer or the retrace ends a frame (machine.h, mach_frame -> plat_wait_frame). */
#pragma once
#include <stdbool.h>

#define APP_FRAME_HZ 70.086   /* VGA mode 13h: 449 lines of 800 dots at 25.175 MHz */
#define APP_AUDIO_RATE 44100

bool app_init(void);        /* data mounted: check the exe; false = fatal (logged) */
int app_main(void);         /* the program, to its exit (plat_exit) */
void app_audio(float *stereo, unsigned frames);
void app_exit(void);
