/* File layer core (see vfs.h): mount dispatch and the FAT disk-image reader. No host file APIs here;
   vfs_host.c adds directories and image files. */
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct VfsFile { void *h; uint64_t size; };

static VfsBackend mnt;
static bool mounted;
static char mnt_desc[512];

void vfs_unmount(void)
{
    if (mounted && mnt.unmount) mnt.unmount(mnt.ctx);
    memset(&mnt, 0, sizeof mnt);
    mounted = false;
    mnt_desc[0] = 0;
}

void vfs_mount(const VfsBackend *b, const char *desc)
{
    vfs_unmount();
    mnt = *b;
    mounted = true;
    snprintf(mnt_desc, sizeof mnt_desc, "%s", desc ? desc : "");
}

bool vfs_mounted(void) { return mounted; }
const char *vfs_describe(void) { return mnt_desc; }

/* "." and ".." components resolved; ".." at the root stays there (from OpenBallance, whose paths were
   relative to a subdirectory of the game, e.g. "..\\Text\\Tutorial2.txt": the mount root stands in for
   both) */
static void normalize(const char *rel, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = rel; *p;) {
        while (*p == '/' || *p == '\\') p++;
        const char *q = p;
        while (*q && *q != '/' && *q != '\\') q++;
        size_t len = (size_t)(q - p);
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            while (n > 0 && out[n - 1] != '/') n--;
            if (n > 0) n--;
        } else if (len && !(len == 1 && *p == '.') && n + len + 2 < cap) {
            if (n) out[n++] = '/';
            memcpy(out + n, p, len);
            n += len;
        }
        p = q;
    }
    out[n] = 0;
}

VfsFile *vfs_open(const char *rel)
{
    if (!mounted || !rel) return NULL;
    char norm[1024];
    normalize(rel, norm, sizeof norm);
    rel = norm;
    uint64_t size = 0;
    void *h = mnt.open(mnt.ctx, rel, &size);
    if (!h) return NULL;
    VfsFile *f = malloc(sizeof *f);
    if (!f) { mnt.close(mnt.ctx, h); return NULL; }
    f->h = h;
    f->size = size;
    return f;
}

uint64_t vfs_file_size(const VfsFile *f) { return f ? f->size : 0; }

int64_t vfs_read_at(VfsFile *f, uint64_t off, void *dst, size_t len)
{
    if (!f) return -1;
    if (off >= f->size || !len) return 0;
    if (len > f->size - off) len = (size_t)(f->size - off);
    return mnt.read_at(mnt.ctx, f->h, off, dst, len);
}

void vfs_close(VfsFile *f)
{
    if (!f) return;
    if (mounted) mnt.close(mnt.ctx, f->h);
    free(f);
}

int64_t vfs_size(const char *rel)
{
    VfsFile *f = vfs_open(rel);
    if (!f) return -1;
    int64_t n = (int64_t)f->size;
    vfs_close(f);
    return n;
}

bool vfs_exists(const char *rel) { return vfs_size(rel) >= 0; }

uint8_t *vfs_read_all(const char *rel, size_t *size)
{
    VfsFile *f = vfs_open(rel);
    if (!f) return NULL;
    size_t n = (size_t)f->size;
    uint8_t *buf = malloc(n ? n : 1);
    bool ok = buf != NULL;
    for (size_t got = 0; ok && got < n; ) {
        int64_t r = vfs_read_at(f, got, buf + got, n - got);
        if (r <= 0) ok = false;
        else got += (size_t)r;
    }
    vfs_close(f);
    if (!ok) { free(buf); return NULL; }
    if (size) *size = n;
    return buf;
}

bool vfs_list(const char *dir, VfsListFn fn, void *user)
{
    return mounted && mnt.list && mnt.list(mnt.ctx, dir ? dir : "", fn, user);
}

/* ------------------------------------------------------------------ FAT12 / FAT16 */

/* The tree is read once at mount time (the floppy has 165 entries). Files keep their cluster chains. */
typedef struct { char name[13]; int parent; bool dir; uint16_t cluster; uint32_t size; } FatEntry;
typedef struct {
    VfsSource src;
    uint32_t bps, spc;        /* bytes per sector, sectors per cluster */
    uint64_t fat_off, root_off, data_off;
    uint32_t root_entries, nclusters;
    bool fat16;
    uint8_t *fat;             /* the first FAT, whole */
    size_t fat_len;
    FatEntry *e;
    int n, cap;
    char label[12];
} Fat;
typedef struct { uint32_t size; uint32_t n; uint16_t chain[]; } FatFile;

static uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* The FAT entry of cluster c: 12 bits packed two in three bytes, or 16. Values >= 0xff8 (0xfff8) end a
   chain; 0, 1 and 0xff7 (bad) are never followed. */
static uint32_t fat_next(const Fat *f, uint32_t c)
{
    if (f->fat16) return (size_t)c * 2 + 1 < f->fat_len ? rd16le(f->fat + c * 2) : 0xffff;
    size_t at = (size_t)c * 3 / 2;
    if (at + 1 >= f->fat_len) return 0xfff;
    uint16_t v = rd16le(f->fat + at);
    return c & 1 ? v >> 4 : v & 0xfff;
}

static bool fat_end(const Fat *f, uint32_t v) { return f->fat16 ? v >= 0xfff8 : v >= 0xff8; }

/* The chain from cluster c, at most max clusters (a loop or a broken chain stops it): count, or -1. */
static int fat_chain(const Fat *f, uint32_t c, uint16_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (c >= 2 && c < f->nclusters + 2 && !fat_end(f, c)) {
        if (n == max) return -1;
        if (out) out[n] = (uint16_t)c;
        n++;
        c = fat_next(f, c);
    }
    return (int)n;
}

static uint64_t cluster_off(const Fat *f, uint32_t c) { return f->data_off + (uint64_t)(c - 2) * f->spc * f->bps; }

static bool fat_add(Fat *f, const FatEntry *e)
{
    if (f->n >= 65536) return false;
    if (f->n == f->cap) {
        int c = f->cap ? f->cap * 2 : 256;
        FatEntry *ne = realloc(f->e, (size_t)c * sizeof *ne);
        if (!ne) return false;
        f->e = ne;
        f->cap = c;
    }
    f->e[f->n++] = *e;
    return true;
}

/* 32-byte directory entries -> children of dir. Deleted (0xe5), volume labels, long-name parts (attribute
   0x0f), "." and ".." are skipped; 0 ends the directory. */
static bool fat_parse_dir(Fat *f, int dir, const uint8_t *d, size_t len)
{
    for (size_t p = 0; p + 32 <= len; p += 32) {
        const uint8_t *r = d + p;
        if (r[0] == 0) break;
        if (r[0] == 0xe5 || r[0] == '.' || r[11] == 0x0f) continue;
        if (r[11] & 0x08) {
            if (dir == 0 && !f->label[0]) {
                int k = 11;
                while (k > 0 && r[k - 1] == ' ') k--;
                memcpy(f->label, r, (size_t)k);
                f->label[k] = 0;
            }
            continue;
        }
        FatEntry e = { .parent = dir, .dir = (r[11] & 0x10) != 0, .cluster = rd16le(r + 26), .size = rd32le(r + 28) };
        int k = 0;
        for (int i = 0; i < 8 && r[i] != ' '; i++) e.name[k++] = (char)(i == 0 && r[0] == 0x05 ? 0xe5 : r[i]);
        if (r[8] != ' ') {
            e.name[k++] = '.';
            for (int i = 8; i < 11 && r[i] != ' '; i++) e.name[k++] = (char)r[i];
        }
        e.name[k] = 0;
        if (e.dir) e.size = 0;
        if (k && !fat_add(f, &e)) return false;
    }
    return true;
}

static bool fat_read_dir(Fat *f, int dir)
{
    if (dir == 0) {
        size_t len = (size_t)f->root_entries * 32;
        uint8_t *d = malloc(len);
        bool ok = d && f->src.read_at(f->src.ctx, f->root_off, d, len) == (int64_t)len && fat_parse_dir(f, 0, d, len);
        free(d);
        return ok;
    }
    uint16_t chain[4096];
    int n = fat_chain(f, f->e[dir].cluster, chain, 4096);
    if (n < 0) return false;
    size_t cl = (size_t)f->spc * f->bps, len = (size_t)n * cl;
    uint8_t *d = malloc(len ? len : 1);
    bool ok = d != NULL;
    for (int i = 0; ok && i < n; i++)
        ok = f->src.read_at(f->src.ctx, cluster_off(f, chain[i]), d + (size_t)i * cl, cl) == (int64_t)cl;
    ok = ok && fat_parse_dir(f, dir, d, len);
    free(d);
    return ok;
}

static int fat_child(const Fat *f, int dir, const char *name, size_t len)
{
    for (int i = 1; i < f->n; i++)
        if (f->e[i].parent == dir && strlen(f->e[i].name) == len && !strncasecmp(f->e[i].name, name, len)) return i;
    return -1;
}

static int fat_lookup(const Fat *f, const char *rel)
{
    int cur = 0;
    const char *p = rel;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        const char *q = p;
        while (*q && *q != '/' && *q != '\\') q++;
        if (!f->e[cur].dir) return -1;
        cur = fat_child(f, cur, p, (size_t)(q - p));
        if (cur < 0) return -1;
        p = q;
    }
    return cur;
}

static void *fat_open(void *ctx, const char *rel, uint64_t *size)
{
    Fat *f = ctx;
    int i = fat_lookup(f, rel);
    if (i < 0 || f->e[i].dir) return NULL;
    uint32_t cl = f->spc * f->bps, need = (f->e[i].size + cl - 1) / cl;
    int n = fat_chain(f, f->e[i].cluster, NULL, f->nclusters);
    if (n < 0 || (uint32_t)n < need) return NULL;              /* a chain shorter than the size: broken */
    FatFile *h = malloc(sizeof *h + (size_t)(need ? need : 1) * sizeof h->chain[0]);
    if (!h) return NULL;
    fat_chain(f, f->e[i].cluster, h->chain, need ? need : 1);
    h->size = f->e[i].size;
    h->n = need;
    *size = h->size;
    return h;
}

static int64_t fat_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    const Fat *f = ctx;
    const FatFile *h = file;
    uint32_t cl = f->spc * f->bps;
    size_t done = 0;
    while (done < len) {
        uint64_t p = off + done;
        uint32_t k = (uint32_t)(p / cl), in = (uint32_t)(p % cl);
        if (k >= h->n) break;
        size_t n = cl - in;
        if (n > len - done) n = len - done;
        int64_t r = f->src.read_at(f->src.ctx, cluster_off(f, h->chain[k]) + in, (uint8_t *)dst + done, n);
        if (r != (int64_t)n) return done ? (int64_t)done : -1;
        done += n;
    }
    return (int64_t)done;
}

static void fat_close(void *ctx, void *file) { free(file); }

static bool fat_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    Fat *f = ctx;
    int d = fat_lookup(f, dir);
    if (d < 0 || !f->e[d].dir) return false;
    for (int i = 1; i < f->n; i++)
        if (f->e[i].parent == d) fn(f->e[i].name, f->e[i].dir, user);
    return true;
}

static void fat_unmount(void *ctx)
{
    Fat *f = ctx;
    if (f->src.close) f->src.close(f->src.ctx);
    free(f->fat);
    free(f->e);
    free(f);
}

bool vfs_mount_image(const VfsSource *src, const char *desc)
{
    Fat *f = calloc(1, sizeof *f);
    if (!f) { if (src->close) src->close(src->ctx); return false; }
    f->src = *src;
    uint8_t b[512];
    bool ok = src->read_at(src->ctx, 0, b, sizeof b) == (int64_t)sizeof b;
    /* The BIOS parameter block (DOS 2.0+): bytes per sector, sectors per cluster, reserved sectors, FATs,
       root entries, total sectors (16-bit, else the 32-bit field), media, sectors per FAT. */
    uint32_t bps = ok ? rd16le(b + 11) : 0, spc = ok ? b[13] : 0, res = ok ? rd16le(b + 14) : 0, nfats = ok ? b[16] : 0;
    uint32_t roots = ok ? rd16le(b + 17) : 0, total = ok ? rd16le(b + 19) : 0, spf = ok ? rd16le(b + 22) : 0;
    if (ok && !total) total = rd32le(b + 32);
    ok = ok && (bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) && spc && !(spc & (spc - 1)) && res &&
         nfats >= 1 && nfats <= 2 && roots && spf && (uint64_t)total * bps <= src->size + bps - 1;
    if (ok) {
        uint32_t root_secs = (roots * 32 + bps - 1) / bps, data_sec = res + nfats * spf + root_secs;
        ok = data_sec < total;
        if (ok) {
            f->bps = bps, f->spc = spc, f->root_entries = roots;
            f->fat_off = (uint64_t)res * bps;
            f->root_off = (uint64_t)(res + nfats * spf) * bps;
            f->data_off = (uint64_t)data_sec * bps;
            f->nclusters = (total - data_sec) / spc;
            f->fat16 = f->nclusters >= 4085;
            ok = f->nclusters < 65525;                          /* FAT32 is not a floppy */
            f->fat_len = (size_t)spf * bps;
            f->fat = ok ? malloc(f->fat_len) : NULL;
            ok = f->fat && src->read_at(src->ctx, f->fat_off, f->fat, f->fat_len) == (int64_t)f->fat_len;
        }
    }
    FatEntry root = { .name = "", .parent = -1, .dir = true };
    ok = ok && fat_add(f, &root);
    for (int i = 0; ok && i < f->n; i++)                       /* breadth first: children are appended */
        if (f->e[i].dir) ok = fat_read_dir(f, i);
    if (!ok) { fat_unmount(f); return false; }
    char d[512];
    snprintf(d, sizeof d, "%s (FAT%d, %u KB%s%s)", desc ? desc : "disk image", f->fat16 ? 16 : 12,
             (unsigned)(src->size / 1024), f->label[0] ? ", label " : "", f->label);
    VfsBackend bk = { f, fat_open, fat_read_at, fat_close, fat_list, fat_unmount };
    vfs_mount(&bk, d);
    return true;
}
