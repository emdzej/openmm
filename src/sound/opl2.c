/* The AdLib's YM3812 (OPL2), modelled from the chip's documented behaviour (docs/re/sound.md, "The
   AdLib"; sources in docs/credits.md): nine two-operator channels, the registers as the driver writes
   them, run at the chip's own rate (3.579545 MHz / 72 = 49,716 Hz) and resampled to the output rate.

   An operator: a phase generator (F-number, block, the multiplier, vibrato), a sine table with the four
   waveforms, an envelope generator (attack, decay to the sustain level, sustain or the percussive decay,
   release; rates scaled by the key), the total level, key-scale level and tremolo as attenuation in
   0.1875 dB steps. A channel: FM (operator 1 modulates operator 2) or additive, with operator 1's
   feedback. Rhythm mode is modelled simply (the drum voices from channels 6-8, the cymbal and hi-hat
   from a noise source). The timers and the status register are not modelled: the game hands the driver
   the port, so the driver never detects the chip (DRIVER1:00e8).

   Not bit-exact with the chip's logarithmic arithmetic (the sine and the attenuation are computed in
   floating point); the envelope's timing, the rates and the levels follow the chip. */
#include "sound.h"
#include <math.h>
#include <string.h>

#define CHIP_HZ (3579545.0 / 72.0)
#define PI 3.14159265358979323846

typedef struct {
    /* registers */
    bool am, vib, egt, ksr;
    uint8_t mult, ksl, tl, ar, dr, sl, rr, ws;
    /* state */
    uint32_t phase;       /* 20-bit: 10 bits of the wave's index, 10 of fraction */
    int att;              /* envelope attenuation, 0 (loudest) .. 511, 0.1875 dB steps */
    enum { EG_OFF, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE } eg;
    bool key;
    float out, prev;      /* the last two outputs (operator 1's feedback) */
} Op;

typedef struct {
    uint16_t fnum;
    uint8_t block, fb;
    bool con, key;
    Op *op[2];
} Chan;

static Op ops[18];
static Chan chans[9];
static uint8_t regs[256];
static uint8_t index_reg;
static bool wave_enable, note_sel, am_deep, vib_deep, rhythm;
static uint32_t eg_counter, lfo_counter, noise = 1;
static double frac;       /* resampling: chip samples per output sample, carried */
static float last, cur;   /* the chip's previous and current sample (linear interpolation) */
static Opl2Log logger;

void opl2_set_log(Opl2Log f) { logger = f; }

/* The operator at register offset o (0-15h; 6, 7, 0Eh, 0Fh unused): slot numbers as the chip's. */
static Op *op_at(uint8_t o)
{
    static const int8_t slot[0x16] = {0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, 12, 13, 14, 15, 16, 17};
    return o < 0x16 && slot[o] >= 0 ? &ops[slot[o]] : NULL;
}

void opl2_reset(void)
{
    memset(ops, 0, sizeof ops);
    memset(chans, 0, sizeof chans);
    memset(regs, 0, sizeof regs);
    index_reg = 0;
    wave_enable = note_sel = am_deep = vib_deep = rhythm = false;
    eg_counter = lfo_counter = 0;
    noise = 1;
    frac = 0, last = cur = 0;
    /* channel c: operators at slots (c % 3) + 6 (c / 3) and +3 */
    for (int c = 0; c < 9; c++) {
        int s = c % 3 + 6 * (c / 3);
        chans[c].op[0] = &ops[s];
        chans[c].op[1] = &ops[s + 3];
    }
    for (int i = 0; i < 18; i++) ops[i].att = 511, ops[i].eg = EG_OFF;
}

static void key_on(Op *o)
{
    if (!o->key) {
        o->key = true;
        o->phase = 0;
        o->eg = EG_ATTACK;
    }
}

static void key_off(Op *o)
{
    if (o->key) {
        o->key = false;
        if (o->eg != EG_OFF) o->eg = EG_RELEASE;
    }
}

void opl2_write_index(uint8_t v) { index_reg = v; }

void opl2_write_data(uint8_t v)
{
    uint8_t r = index_reg;
    if (logger) logger(r, v);
    regs[r] = v;
    Op *o;
    switch (r & 0xe0) {
    case 0x20:
        if ((o = op_at(r & 0x1f))) o->am = v & 0x80, o->vib = v & 0x40, o->egt = v & 0x20, o->ksr = v & 0x10, o->mult = v & 15;
        return;
    case 0x40:
        if ((o = op_at(r & 0x1f))) o->ksl = v >> 6, o->tl = v & 0x3f;
        return;
    case 0x60:
        if ((o = op_at(r & 0x1f))) o->ar = v >> 4, o->dr = v & 15;
        return;
    case 0x80:
        if ((o = op_at(r & 0x1f))) o->sl = v >> 4, o->rr = v & 15;
        return;
    case 0xe0:
        if ((o = op_at(r & 0x1f))) o->ws = v & 3;
        return;
    default: break;
    }
    if (r == 0x01) wave_enable = v & 0x20;
    else if (r == 0x08) note_sel = v & 0x40;
    else if (r == 0xbd) {
        am_deep = v & 0x80, vib_deep = v & 0x40, rhythm = v & 0x20;
        if (rhythm) {   /* bass drum (channel 6), snare (7.2), tom (8.1), cymbal (8.2), hi-hat (7.1) */
            Op *d[5][2] = {{chans[6].op[0], chans[6].op[1]}, {chans[7].op[1], NULL}, {chans[8].op[0], NULL},
                           {chans[8].op[1], NULL}, {chans[7].op[0], NULL}};
            for (int i = 0; i < 5; i++)
                for (int k = 0; k < 2; k++)
                    if (d[i][k]) ((v >> (4 - i)) & 1) ? key_on(d[i][k]) : key_off(d[i][k]);
        }
    } else if (r >= 0xa0 && r <= 0xa8) {
        chans[r - 0xa0].fnum = (uint16_t)((chans[r - 0xa0].fnum & 0x300) | v);
    } else if (r >= 0xb0 && r <= 0xb8) {
        Chan *c = &chans[r - 0xb0];
        c->fnum = (uint16_t)((c->fnum & 0xff) | (v & 3) << 8);
        c->block = (v >> 2) & 7;
        bool k = v & 0x20;
        if (k != c->key) {
            c->key = k;
            for (int i = 0; i < 2; i++) k ? key_on(c->op[i]) : key_off(c->op[i]);
        }
    } else if (r >= 0xc0 && r <= 0xc8) {
        chans[r - 0xc0].fb = (v >> 1) & 7;
        chans[r - 0xc0].con = v & 1;
    }
}

uint8_t opl2_read_status(void) { return 0; }

/* ---- one chip sample ---- */

static const uint8_t mult_x2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
/* key-scale level: the attenuation (0.75 dB steps) at block 7 by the F-number's top four bits */
static const uint8_t ksl_base[16] = {0, 24, 32, 37, 40, 43, 45, 47, 48, 50, 51, 52, 53, 54, 55, 56};
/* the envelope's increments in each group of eight steps, by the rate's low two bits */
static const uint8_t eg_pattern[4][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}};

static int keycode(const Chan *c) { return c->block << 1 | ((c->fnum >> (note_sel ? 8 : 9)) & 1); }

static int eff_rate(const Op *o, const Chan *c, int r)
{
    if (r == 0) return 0;
    int k = keycode(c);
    int v = 4 * r + (o->ksr ? k : k >> 2);
    return v > 63 ? 63 : v;
}

/* the envelope's step this sample at effective rate r: 0 or the increment */
static int eg_step(int r)
{
    if (r == 0) return 0;
    int hi = r >> 2;
    if (hi < 12) {
        int shift = 12 - hi;
        if (eg_counter & ((1u << shift) - 1)) return 0;
        return eg_pattern[r & 3][(eg_counter >> shift) & 7];
    }
    return (eg_pattern[r & 3][eg_counter & 7] + 1) << (hi - 12 > 2 ? 2 : hi - 12);
}

static void envelope(Op *o, const Chan *c)
{
    switch (o->eg) {
    case EG_ATTACK: {
        int r = eff_rate(o, c, o->ar);
        if (r >= 60) o->att = 0;
        else {
            int inc = eg_step(r);
            if (inc) {
                int d = -(o->att + 1) * inc;
                o->att += d >= 0 ? d >> 3 : -((-d + 7) >> 3);
                if (o->att < 0) o->att = 0;
            }
        }
        if (o->att == 0) o->eg = EG_DECAY;
        break;
    }
    case EG_DECAY: {
        int sl = o->sl == 15 ? 511 : o->sl << 4;
        o->att += eg_step(eff_rate(o, c, o->dr));
        if (o->att >= sl) o->att = sl, o->eg = EG_SUSTAIN;
        break;
    }
    case EG_SUSTAIN:
        if (!o->egt) o->att += eg_step(eff_rate(o, c, o->rr));   /* percussive: decays on at the release rate */
        break;
    case EG_RELEASE:
        o->att += eg_step(eff_rate(o, c, o->rr));
        break;
    default: break;
    }
    if (o->att >= 511) {
        o->att = 511;
        if (o->eg == EG_RELEASE || (o->eg == EG_SUSTAIN && !o->egt)) o->eg = EG_OFF;
    }
}

/* the wave at phase p (10 bits a cycle) for waveform w, -1..1 */
static float wave(uint32_t p, uint8_t w)
{
    p &= 1023;
    float s = (float)sin((p + 0.5) * (2 * PI / 1024));
    switch (wave_enable ? w : 0) {
    case 1: return p < 512 ? s : 0;                 /* half sine */
    case 2: return fabsf(s);                        /* absolute sine */
    case 3: return (p & 256) ? 0 : fabsf(s);        /* the rising quarters only */
    default: return s;
    }
}

static float op_out(Op *o, const Chan *c, int am, int vib, float mod)
{
    /* phase: F-number with vibrato, << block, times the multiplier */
    int f = c->fnum;
    if (o->vib) {
        static const int8_t vt[8] = {0, 1, 2, 1, 0, -1, -2, -1};
        f += (vt[vib] * (f >> 7)) >> (vib_deep ? 0 : 1);
    }
    uint32_t inc = (uint32_t)(((uint32_t)f << c->block) * mult_x2[o->mult]) >> 1;
    o->phase = (o->phase + inc) & 0xfffff;
    if (o->eg == EG_OFF) return 0;
    int ksl = 0;
    if (o->ksl) {
        int k = ksl_base[c->fnum >> 6] - 8 * (7 - c->block);
        if (k > 0) ksl = (k * 4) >> (o->ksl == 1 ? 1 : o->ksl == 2 ? 2 : 0);
    }
    int att = o->att + o->tl * 4 + ksl + (o->am ? (am_deep ? am : am >> 2) : 0);
    if (att >= 511) return 0;
    float amp = (float)pow(10.0, -att * 0.1875 / 20.0);
    uint32_t p = (o->phase >> 10) + (uint32_t)(int32_t)lrintf(mod * 1024.0f);
    return wave(p, o->ws) * amp;
}

static float chip_sample(void)
{
    eg_counter++;
    lfo_counter++;
    /* tremolo: a triangle 0..26 steps over 13,440 samples (3.7 Hz); vibrato: eight steps over 8,192 (6.1 Hz) */
    int t = (int)((lfo_counter % 13440) * 52 / 13440);
    int am = t <= 26 ? t : 52 - t;
    int vib = (int)((lfo_counter >> 10) & 7);
    noise = (noise >> 1) ^ ((noise & 1) ? 0x400181u : 0);
    float mix = 0;
    int nmelodic = rhythm ? 6 : 9;
    for (int ci = 0; ci < 9; ci++) {
        Chan *c = &chans[ci];
        Op *m = c->op[0], *s = c->op[1];
        envelope(m, c);
        envelope(s, c);
        if (ci < nmelodic) {
            float fbk = c->fb ? (m->out + m->prev) * (float)(1 << c->fb) / 128.0f : 0;
            float a = op_out(m, c, am, vib, fbk);
            m->prev = m->out, m->out = a;
            float b = op_out(s, c, am, vib, c->con ? 0 : a * 2);
            mix += c->con ? a + b : b;
        } else if (ci == 6) {   /* bass drum: a normal FM pair, twice as loud */
            float fbk = c->fb ? (m->out + m->prev) * (float)(1 << c->fb) / 128.0f : 0;
            float a = op_out(m, c, am, vib, fbk);
            m->prev = m->out, m->out = a;
            mix += 2 * op_out(s, c, am, vib, c->con ? 0 : a * 2);
        } else {                /* 7: hi-hat (op 1) and snare (op 2); 8: tom (op 1) and cymbal (op 2) */
            float n = (noise & 1) ? 1.0f : -1.0f;
            float x = op_out(m, c, am, vib, 0), y = op_out(s, c, am, vib, 0);
            if (ci == 7) mix += 2 * (fabsf(x) * n) + 2 * (y * 0.5f + fabsf(y) * n * 0.5f);
            else mix += 2 * x + 2 * (fabsf(y) * n);
        }
    }
    return mix / 9.0f;
}

void opl2_render(float *out, unsigned n, double rate)
{
    double step = CHIP_HZ / rate;
    for (unsigned i = 0; i < n; i++) {
        frac += step;
        while (frac >= 1.0) {
            last = cur;
            cur = chip_sample();
            frac -= 1.0;
        }
        out[i] += last + (cur - last) * (float)frac;
    }
}
