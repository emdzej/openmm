/* The port headless (native), for comparing with the reference harness frame by frame: the program runs
   from its entry with scripted keys; each frame's picture (the 320 x 200 indices and the DAC) is hashed,
   and the memory can be written at chosen frames (git-ignored out/: it holds the program's data).
     build/mm_run [--data game] [--frames 300] [--keys 120:1c,124:9c] [--hashes] [--dump 100,200]
                  [--shot 100] [--out out/run]
   --keys: frame:scancode in hex (80h set = release, 100h = the E0 prefix); --hashes: one line per frame,
   "frame fnv32"; --param name=value as gasm's (splash=0: no SM.EXE, codecard=1); --dump: out/run/mem_<frame>.bin; --shot: a PPM of the port's frame; --wav: the sound,
   44.1 kHz 16-bit stereo. The game's settings (SETTINGS.DAT, as the user's storage) are read from
   <out>/writes/ first, so a run can choose the sound device. */
#include "app.h"
#include "platform.h"
#include "vfs.h"
#include "vfs_host.h"
#include "mem.h"
#include "sound/sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int frames_max = 300, frame;
static bool hashes;
static const char *out_dir = "out/run";
static int dumps[64], ndumps, shots[64], nshots;
static struct { int frame; uint16_t code; } keys[1024];
static int nkeys;
static uint8_t last_px[320 * 200], last_pal[768];
static uint32_t video_hash = 2166136261u, audio_hash = 2166136261u;
static FILE *wav, *opl_log;
static void log_opl(uint8_t idx, uint8_t v) { fprintf(opl_log, "%d %x %x\n", frame, idx, v); }
static uint32_t wav_frames;
static double audio_acc;

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void plat_log(const char *msg) { printf("%s\n", msg); }
double plat_ms(void) { return frame * (1000.0 / APP_FRAME_HZ); }
uint32_t plat_pad(int player) { return 0; }
bool plat_gamepad(int slot, PlatGamepad *g) { memset(g, 0, sizeof *g); return false; }
static const char *params[16];
static int nparams;
bool plat_param(const char *name, char *dst, size_t cap)
{
    size_t n = strlen(name);
    for (int i = 0; i < nparams; i++)
        if (!strncmp(params[i], name, n) && params[i][n] == '=') {
            snprintf(dst, cap, "%s", params[i] + n + 1);
            return true;
        }
    return false;
}
void plat_set_frame_rate(double hz) {}
uint8_t *plat_load_user_file(const char *name, size_t *size)
{
    char p[512];
    snprintf(p, sizeof p, "%s/writes/%s", out_dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    *size = fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}
bool plat_save_user_file(const char *name, const void *data, size_t size)
{
    char p[512];
    snprintf(p, sizeof p, "%s/writes", out_dir);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/writes/%s", out_dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    fwrite(data, 1, size, f);
    fclose(f);
    return true;
}

void plat_present(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768])
{
    for (int y = 0; y < h && y < 200; y++) memcpy(last_px + y * 320, pixels + y * pitch, (size_t)(w < 320 ? w : 320));
    memcpy(last_pal, pal, 768);
}

int plat_key_events(uint16_t *codes, int cap)
{
    int n = 0;
    for (int i = 0; i < nkeys && n < cap; i++)
        if (keys[i].frame == frame) codes[n++] = keys[i].code;
    return n;
}

static void finish(void)
{
    printf("frames %d, video_fnv32=%08x audio_fnv32=%08x\n", frame, video_hash, audio_hash);
    if (wav) {   /* the RIFF header's sizes */
        uint32_t data = wav_frames * 4, riff = 36 + data;
        fseek(wav, 4, SEEK_SET), fwrite(&riff, 4, 1, wav);
        fseek(wav, 40, SEEK_SET), fwrite(&data, 4, 1, wav);
        fclose(wav), wav = NULL;
    }
}

void plat_exit(int code)
{
    finish();
    printf("exit %d at frame %d\n", code, frame);
    app_exit();
    exit(code);
}

void plat_wait_frame(void)
{
    static float buf[4096 * 2];
    audio_acc += APP_AUDIO_RATE / APP_FRAME_HZ;
    unsigned na = (unsigned)audio_acc;
    audio_acc -= na;
    app_audio(buf, na);
    audio_hash = fnv(audio_hash, (const uint8_t *)buf, na * 2 * sizeof *buf);
    if (wav)
        for (unsigned i = 0; i < na; i++) {
            int16_t lr[2] = {(int16_t)(buf[2 * i] * 32000), (int16_t)(buf[2 * i + 1] * 32000)};
            fwrite(lr, 2, 2, wav);
            wav_frames++;
        }
    uint32_t h = fnv(fnv(2166136261u, last_px, sizeof last_px), last_pal, sizeof last_pal);
    video_hash = fnv(video_hash, (const uint8_t *)&h, 4);
    if (hashes) printf("%d %08x\n", frame, h);
    for (int i = 0; i < nshots; i++)
        if (shots[i] == frame) {
            char p[512];
            snprintf(p, sizeof p, "%s/f%05d.ppm", out_dir, frame);
            FILE *f = fopen(p, "wb");
            if (f) {
                fprintf(f, "P6 320 200 255\n");
                for (int k = 0; k < 320 * 200; k++) fwrite(last_pal + last_px[k] * 3, 1, 3, f);
                fclose(f);
            }
        }
    for (int i = 0; i < ndumps; i++)
        if (dumps[i] == frame) {
            char p[512];
            snprintf(p, sizeof p, "%s/mem_%d.bin", out_dir, frame);
            FILE *f = fopen(p, "wb");
            if (f) fwrite(g_mem, 1, MEM_SIZE, f), fclose(f);
        }
    frame++;
    if (frame >= frames_max) {
        finish();
        app_exit();
        exit(0);
    }
}

static int list(const char *s, int *dst, int cap)
{
    int n = 0;
    while (s && *s && n < cap) {
        dst[n++] = atoi(s);
        s = strchr(s, ',');
        if (s) s++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *data = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--data") && v) data = v, i++;
        else if (!strcmp(a, "--frames") && v) frames_max = atoi(v), i++;
        else if (!strcmp(a, "--hashes")) hashes = true;
        else if (!strcmp(a, "--param") && v && nparams < 16) params[nparams++] = v, i++;
        else if (!strcmp(a, "--dump") && v) ndumps = list(v, dumps, 64), i++;
        else if (!strcmp(a, "--shot") && v) nshots = list(v, shots, 64), i++;
        else if (!strcmp(a, "--out") && v) out_dir = v, i++;
        else if (!strcmp(a, "--opl-log") && v) {
            opl_log = fopen(v, "w");
            if (opl_log) opl2_set_log(log_opl);
            i++;
        } else if (!strcmp(a, "--wav") && v) {
            wav = fopen(v, "wb");
            static const uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                                            0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
            if (wav) fwrite(hdr, 1, sizeof hdr, wav);
            i++;
        }
        else if (!strcmp(a, "--keys") && v) {
            for (const char *s = v; *s && nkeys < 1024;) {
                int f = atoi(s);
                const char *c = strchr(s, ':');
                if (!c) break;
                keys[nkeys].frame = f, keys[nkeys].code = (uint16_t)strtol(c + 1, NULL, 16), nkeys++;
                s = strchr(c, ',');
                if (!s) break;
                s++;
            }
            i++;
        } else {
            fprintf(stderr, "usage: mm_run [--data dir|image] [--frames n] [--keys f:hex,...] [--hashes] "
                            "[--dump f,...] [--shot f,...] [--out dir]\n");
            return 2;
        }
    }
    mkdir("out", 0755);
    mkdir(out_dir, 0755);
    if (data) setenv("OPENMM_DATA", data, 1);
    if (!vfs_mount_default()) {
        printf("mm_run: game data not found\n");
        return 1;
    }
    if (!app_init()) return 1;
    return app_main();
}
