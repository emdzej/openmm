/* The platform for the native tests and tools: the log to stdout, no screen, no input, no storage. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>

void plat_log(const char *msg) { printf("%s\n", msg); }
double plat_ms(void) { return 0; }
uint32_t plat_pad(int player) { return 0; }
bool plat_gamepad(int slot, PlatGamepad *g) { return false; }
int plat_key_events(uint16_t *codes, int cap) { return 0; }
bool plat_param(const char *name, char *dst, size_t cap) { return false; }
void plat_set_frame_rate(double hz) {}
void plat_present(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768]) {}
void plat_wait_frame(void) {}
void plat_exit(int code) { exit(code); }
uint8_t *plat_load_user_file(const char *name, size_t *size) { return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { return false; }
