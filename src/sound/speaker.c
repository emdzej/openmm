/* The PC speaker (docs/re/sound.md): the PIT's channel 2 in mode 3 (a square wave at 1,193,182 Hz /
   the count, written through port 42h after 43h = B6h), heard while port 61h has both its gate (bit 0)
   and the speaker's data (bit 1) set. The drivers only start, change and stop tones (they never toggle
   the speaker by hand), and the port's CPU is instant, so the state at the end of each frame is what
   sounds for the next frame's samples; a tone on and off within one frame (DRIVER1's wait after an
   AdLib write) is a few microseconds on the real machine and not heard. */
#include "sound.h"

static uint16_t count = 0;    /* channel 2's reload (0 = 65536) */
static int lo = -1;           /* the low byte of a reload being written */
static uint8_t port61;
static double phase;

void speaker_reset(void) { count = 0, lo = -1, port61 = 0, phase = 0; }

void speaker_out(uint16_t port, uint8_t v)
{
    if (port == 0x43) {
        if ((v >> 6) == 2 && ((v >> 4) & 3) != 0) lo = -1;   /* channel 2, a new reload follows */
    } else if (port == 0x42) {
        if (lo < 0) lo = v;
        else count = (uint16_t)(v << 8 | lo), lo = -1;
    } else if (port == 0x61) {
        port61 = v;
    }
}

uint8_t speaker_port61(void) { return port61; }

void speaker_render(float *out, unsigned n, double rate)
{
    if ((port61 & 3) != 3) return;
    double hz = 1193182.0 / (count ? count : 65536);
    if (hz > rate / 2) return;   /* above what the output can carry: the real speaker barely moves */
    double step = hz / rate;
    for (unsigned i = 0; i < n; i++) {
        out[i] += phase < 0.5 ? 0.25f : -0.25f;
        phase += step;
        if (phase >= 1.0) phase -= 1.0;
    }
}
