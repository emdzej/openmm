/* Platform contract: what the portable core needs from a host (backend: platform_gasm.c). The game data
   comes through the file layer (vfs.h).

   The original is a real-mode DOS program that owns the machine: it draws into VGA memory in a 256-colour
   mode and programs the DAC's palette, takes the keyboard's scan codes from its own IRQ 1 handler, reads
   the game port's joysticks, times itself from the PIT, and plays sound through its Sound Images drivers
   (DRIVER*.BIN). The port keeps each of those as a ported device model in the core (docs/re/port-plan.md)
   and needs only this from the host: an 8-bit frame with its palette, scan codes, controllers, a clock
   of frames, and an audio stream. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Virtual pad buttons, the gasm ABI layout (SNES positions: A east, B south, X north, Y west). */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5,
    PAD_SELECT = 1 << 6, PAD_START = 1 << 7, PAD_UP = 1 << 8, PAD_DOWN = 1 << 9, PAD_LEFT = 1 << 10,
    PAD_RIGHT = 1 << 11,
};

/* Keys are what the original's IRQ 1 handler reads from port 60h: set-1 scan codes, 0x80 set on release.
   Extended keys (the arrows' own block, right Ctrl/Alt, keypad Enter and /) come with KEY_E0, standing for
   the 0xe0 byte the keyboard sends before them. */
enum { KEY_COUNT = 256, KEY_RELEASE = 0x80, KEY_E0 = 0x100 };

void plat_log(const char *msg);
double plat_ms(void);                   /* a monotonic clock in ms (real only in a window under gasm) */
uint32_t plat_pad(int player);          /* stable within a frame */
/* A raw game controller in slot 0-3 (connection order), stable within a frame: buttons 0-1 and axes
   -1..1 (y > 0 down); with standard, the W3C order (buttons 0 south, 1 east, 2 west, 3 north, 4/5
   shoulders, 6/7 triggers, 8 select, 9 start, 10/11 stick clicks, 12-15 d-pad up, down, left, right;
   axes 0/1 left stick, 2/3 right stick). False for an empty slot. */
typedef struct {
    bool standard;
    int nbuttons, naxes;
    float button[32];
    float axis[16];
} PlatGamepad;
bool plat_gamepad(int slot, PlatGamepad *g);
/* The keyboard's events since the previous frame, in order, as the IRQ 1 handler would see them: a
   scan code (| KEY_RELEASE on release, | KEY_E0 for an extended key); count, at most cap. False-free:
   0 if the runner has no keyboard. */
int plat_key_events(uint16_t *codes, int cap);
/* Launch parameter (gasm --param / URL query); false if unset. */
bool plat_param(const char *name, char *dst, size_t cap);
/* Calls per second of app_frame from now on; the audio per frame follows. */
void plat_set_frame_rate(double hz);
/* Show an 8-bit frame: w x h indices, pitch bytes per row, through pal (256 x RGB, 8 bits per channel;
   the VGA DAC's 6-bit values are the caller's to widen). Shown at 4:3, as a 320 x 200 mode is on a CRT.
   The pixels are copied; they may change after the call. */
void plat_present(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768]);
/* Persistent user files (the settings the game writes). */
uint8_t *plat_load_user_file(const char *name, size_t *size);
bool plat_save_user_file(const char *name, const void *data, size_t size);
