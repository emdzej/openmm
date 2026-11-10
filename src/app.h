/* The application per frame. The original is a DOS program with its own loops, timed by its IRQ 0
   handler; gasm calls app_frame once per frame, and the port runs the original's work for one frame of
   its timer there (docs/re/port-plan.md, step 1). A frame never blocks. */
#pragma once
#include <stdbool.h>

#define APP_FRAME_HZ 70.0     /* VGA mode 13h refreshes at 70 Hz; the game's timing is to be confirmed */
#define APP_AUDIO_RATE 44100

bool app_init(void);        /* data mounted: check the exe; false = fatal (logged) */
bool app_frame(void);       /* one frame; false = the game quit */
void app_audio(float *stereo, unsigned frames);
void app_exit(void);
