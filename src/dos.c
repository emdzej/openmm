/* DOS's int 21h as the game calls it (the services of the reference harness, tools/emu/dos.py, which
   lists them): files over the VFS (read-only game data) with the files the game writes (SETTINGS.DAT)
   in the user's storage, read back from there first; the current directory; find first/next; the
   vectors; the date and time. Handles are numbered from 5 as DOS gives them after the five standard
   ones. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "platform.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double mach_time(void);

enum { NFILES = 16, FIRST_HANDLE = 5, PATH_MAX_ = 128 };

typedef struct {
    bool open;
    VfsFile *vf;          /* the game's data, read-only */
    uint8_t *buf;         /* a file in the user's storage (written, or written before), whole */
    size_t size, cap;
    bool dirty;
    uint64_t pos;
    char key[PATH_MAX_];  /* its storage name: the path, '/' as "__" (as the harness names it) */
} DosFile;

static DosFile files[NFILES];
static uint16_t mem_free = 0xa000;   /* the first paragraph above the running program's block */
static bool exited;
static uint8_t exit_code;

void dos_set_program_end(uint16_t seg) { mem_free = seg; }
bool dos_exited(uint8_t *code)
{
    if (code) *code = exit_code;
    bool e = exited;
    exited = false;
    return e;
}
static char cwd[PATH_MAX_];   /* relative to the data root, '/'-separated, upper case */
static uint16_t dta_seg, dta_off;

/* find first/next */
static char find_names[256][13];
static bool find_dirs[256];
static uint32_t find_sizes[256];
static int find_n, find_i;

void dos_reset(void)
{
    for (int i = 0; i < NFILES; i++) {
        free(files[i].buf);
        if (files[i].vf) vfs_close(files[i].vf);
    }
    memset(files, 0, sizeof files);
    cwd[0] = 0;
    dta_seg = SEG_PSP, dta_off = 0x80;
    find_n = find_i = 0;
}

static void cstr(uint16_t seg, uint16_t off, char *dst, size_t cap)
{
    size_t i = 0;
    for (; i + 1 < cap; i++) {
        char c = (char)mb(seg, (uint16_t)(off + i));
        if (!c) break;
        dst[i] = c;
    }
    dst[i] = 0;
}

/* A DOS path against the current directory: drive letters dropped, '\' -> '/', "." and ".." resolved,
   upper case. */
static void resolve(const char *name, char *out, size_t cap)
{
    char parts[16][16];
    int n = 0;
    if (name[0] && name[1] == ':') name += 2;
    const char *src[2] = {name[0] == '\\' || name[0] == '/' ? "" : cwd, name};
    for (int s = 0; s < 2; s++) {
        const char *p = src[s];
        while (*p) {
            char seg[16];
            int k = 0;
            while (*p && *p != '/' && *p != '\\') {
                char c = *p++;
                if (k < 15) seg[k++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
            }
            seg[k] = 0;
            if (*p) p++;
            if (!k || !strcmp(seg, ".")) continue;
            if (!strcmp(seg, "..")) { if (n) n--; continue; }
            if (n < 16) memcpy(parts[n++], seg, 16);
        }
    }
    size_t k = 0;
    for (int i = 0; i < n; i++) {
        if (i && k + 1 < cap) out[k++] = '/';
        for (const char *q = parts[i]; *q && k + 1 < cap; q++) out[k++] = *q;
    }
    out[k] = 0;
}

static void storage_name(const char *rel, char *key, size_t cap)
{
    size_t k = 0;
    for (const char *p = rel; *p && k + 3 < cap; p++) {
        if (*p == '/') key[k++] = '_', key[k++] = '_';
        else key[k++] = *p;
    }
    key[k] = 0;
}

static void list_none(const char *name, bool dir, void *user) {}

static bool is_dir(const char *rel)
{
    if (!rel[0]) return true;
    return vfs_list(rel, list_none, NULL);
}

static DosFile *handle(uint16_t h)
{
    if (h < FIRST_HANDLE || h >= FIRST_HANDLE + NFILES || !files[h - FIRST_HANDLE].open) return NULL;
    return &files[h - FIRST_HANDLE];
}

static int new_handle(void)
{
    for (int i = 0; i < NFILES; i++)
        if (!files[i].open) return i;
    return -1;
}

static void fail(uint16_t code) { R.ax = code, R.cf = true; }

static void flush(DosFile *f)
{
    if (f->dirty) plat_save_user_file(f->key, f->buf, f->size);
    f->dirty = false;
}

static void open_file(bool create, uint8_t mode)
{
    char name[PATH_MAX_], rel[PATH_MAX_];
    cstr(R.ds, R.dx, name, sizeof name);
    resolve(name, rel, sizeof rel);
    int i = new_handle();
    if (i < 0) { fail(4); return; }
    DosFile *f = &files[i];
    memset(f, 0, sizeof *f);
    storage_name(rel, f->key, sizeof f->key);
    size_t n = 0;
    uint8_t *saved = create ? NULL : plat_load_user_file(f->key, &n);
    if (create) {
        f->dirty = true;
    } else if (saved) {
        f->buf = saved, f->size = f->cap = n;
    } else if ((mode & 3) != 0) {   /* opened for writing: a copy of the game's file in the storage */
        f->buf = vfs_read_all(rel, &n);
        if (!f->buf) { fail(2); return; }
        f->size = f->cap = n;
    } else {
        f->vf = vfs_open(rel);
        if (!f->vf) { fail(2); return; }
    }
    f->open = true;
    R.ax = (uint16_t)(FIRST_HANDLE + i), R.cf = false;
}

static void find_add(const char *name, bool dir, void *user)
{
    const char *mask = user;
    if (find_n == 256) return;
    /* the DOS mask: "*.*" any, '*' the rest of the name or the extension, '?' one character */
    const char *n = name, *m = mask;
    char base[9] = "", ext[4] = "", mb_[9] = "", me[4] = "";
    const char *dot = strchr(n, '.');
    snprintf(base, sizeof base, "%.*s", dot ? (int)(dot - n) : 8, n);
    if (dot) snprintf(ext, sizeof ext, "%s", dot + 1);
    const char *mdot = strchr(m, '.');
    snprintf(mb_, sizeof mb_, "%.*s", mdot ? (int)(mdot - m) : 8, m);
    if (mdot) snprintf(me, sizeof me, "%s", mdot + 1);
    const char *pairs[2][2] = {{base, mb_}, {ext, me}};
    for (int k = 0; k < 2; k++) {
        const char *s = pairs[k][0], *p = pairs[k][1];
        for (;; s++, p++) {
            if (*p == '*') break;
            if (!*p) { if (*s) return; break; }
            char a = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s, b = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p;
            if (b != '?' && a != b) return;
            if (!*s && b == '?') return;
        }
    }
    snprintf(find_names[find_n], 13, "%s", name);
    find_dirs[find_n] = dir;
    find_n++;
}

static int cmp_names(const void *a, const void *b) { return strcmp(a, b); }

static void find_next(void)
{
    if (find_i >= find_n) { fail(18); return; }
    uint8_t rec[43] = {0};
    const char *name = find_names[find_i];
    rec[21] = find_dirs[find_i] ? 0x10 : 0x20;
    uint32_t sz = find_sizes[find_i];
    rec[26] = (uint8_t)sz, rec[27] = (uint8_t)(sz >> 8), rec[28] = (uint8_t)(sz >> 16), rec[29] = (uint8_t)(sz >> 24);
    for (int k = 0; name[k] && k < 12; k++) rec[30 + k] = (uint8_t)(name[k] >= 'a' && name[k] <= 'z' ? name[k] - 32 : name[k]);
    mem_write(dta_seg, dta_off, rec, sizeof rec);
    find_i++;
    R.cf = false;
}

/* int 21h */
void dos_int21(void)
{
    uint8_t al = AL;
    switch (AH) {
    case 0x30: R.ax = 0x0005, R.bx = 0, R.cx = 0; break;   /* DOS 5.0 */
    case 0x09: case 0x02: case 0x06: break;                 /* text output: the text screen is not shown */
    case 0x19: SET_AL(2); break;                            /* C: */
    case 0x0e: SET_AL(26); break;
    case 0x1a: dta_seg = R.ds, dta_off = R.dx; break;
    case 0x2f: R.es = dta_seg, R.bx = dta_off; break;
    case 0x25: mww(0, (uint16_t)(al * 4), R.dx), mww(0, (uint16_t)(al * 4 + 2), R.ds); break;
    case 0x35: R.bx = mw(0, (uint16_t)(al * 4)), R.es = mw(0, (uint16_t)(al * 4 + 2)); break;
    case 0x2a: R.cx = 1994, R.dx = 0x090e, SET_AL(3); break;   /* the release date, a Wednesday */
    case 0x2c: {
        uint32_t t = (uint32_t)(mach_time() / 1193182.0 * 100);
        R.cx = (uint16_t)((t / 360000 % 24) << 8 | (t / 6000 % 60));
        R.dx = (uint16_t)((t / 100 % 60) << 8 | t % 100);
        break;
    }
    case 0x2b: case 0x2d: SET_AL(0); break;
    case 0x3b: {
        char name[PATH_MAX_], rel[PATH_MAX_];
        cstr(R.ds, R.dx, name, sizeof name);
        resolve(name, rel, sizeof rel);
        if (is_dir(rel)) snprintf(cwd, sizeof cwd, "%s", rel), R.cf = false;
        else fail(3);
        break;
    }
    case 0x47: {
        char out[PATH_MAX_];
        size_t k = 0;
        for (const char *p = cwd; *p && k + 1 < sizeof out; p++) out[k++] = *p == '/' ? '\\' : *p;
        out[k++] = 0;
        mem_write(R.ds, R.si, out, k);
        R.cf = false;
        break;
    }
    case 0x3c: open_file(true, 2); break;
    case 0x3d: open_file(false, al); break;
    case 0x3e: {
        DosFile *f = handle(R.bx);
        if (!f) { R.cf = true; break; }
        flush(f);
        if (f->vf) vfs_close(f->vf);
        free(f->buf);
        memset(f, 0, sizeof *f);
        R.cf = false;
        break;
    }
    case 0x3f: {
        DosFile *f = handle(R.bx);
        if (!f) { fail(6); break; }
        uint8_t tmp[65536];
        size_t n = R.cx;
        if (f->vf) {
            int64_t got = vfs_read_at(f->vf, f->pos, tmp, n);
            n = got < 0 ? 0 : (size_t)got;
        } else {
            n = f->pos >= f->size ? 0 : (f->size - f->pos < n ? f->size - (size_t)f->pos : n);
            memcpy(tmp, f->buf + f->pos, n);
        }
        mem_write(R.ds, R.dx, tmp, n);
        f->pos += n;
        R.ax = (uint16_t)n, R.cf = false;
        break;
    }
    case 0x40: {
        if (R.bx == 1 || R.bx == 2) { R.ax = R.cx, R.cf = false; break; }
        DosFile *f = handle(R.bx);
        if (!f || f->vf) { fail(f ? 5 : 6); break; }
        size_t end = (size_t)f->pos + R.cx;
        if (end > f->cap) {
            f->cap = end + 4096;
            f->buf = realloc(f->buf, f->cap);
        }
        if (f->pos > f->size) memset(f->buf + f->size, 0, (size_t)f->pos - f->size);
        mem_read(R.ds, R.dx, f->buf + f->pos, R.cx);
        f->pos = end;
        if (end > f->size) f->size = end;
        f->dirty = true;
        R.ax = R.cx, R.cf = false;
        break;
    }
    case 0x42: {
        DosFile *f = handle(R.bx);
        if (!f) { fail(6); break; }
        int64_t off = (int32_t)((uint32_t)R.cx << 16 | R.dx);
        int64_t size = f->vf ? (int64_t)vfs_file_size(f->vf) : (int64_t)f->size;
        int64_t base = al == 0 ? 0 : al == 1 ? (int64_t)f->pos : size;
        int64_t pos = base + off;
        if (pos < 0) pos = 0;
        f->pos = (uint64_t)pos;
        R.dx = (uint16_t)(pos >> 16), R.ax = (uint16_t)pos, R.cf = false;
        break;
    }
    case 0x4a: R.cf = false; break;
    case 0x48:   /* above the program's block, as the harness: MICRO.EXE asks for all memory, SM.EXE not */
        if ((uint32_t)mem_free + R.bx <= 0xa000) R.ax = mem_free, mem_free = (uint16_t)(mem_free + R.bx), R.cf = false;
        else R.ax = 8, R.bx = (uint16_t)(0xa000 - mem_free), R.cf = true;
        break;
    case 0x49: R.cf = false; break;
    case 0x4c:   /* the program ends: its ported code returns from here to the launcher (launcher.c) */
        for (int i = 0; i < NFILES; i++)
            if (files[i].open) flush(&files[i]);
        exited = true, exit_code = al;
        break;
    case 0x4e: {
        char pat[PATH_MAX_], rel[PATH_MAX_], dir[PATH_MAX_];
        cstr(R.ds, R.dx, pat, sizeof pat);
        resolve(pat, rel, sizeof rel);
        const char *slash = strrchr(rel, '/');
        snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - rel) : 0, rel);
        const char *mask = slash ? slash + 1 : rel;
        find_n = find_i = 0;
        vfs_list(dir, find_add, (void *)mask);
        /* sorted by name, as the harness lists a folder (a floppy lists in directory order) */
        qsort(find_names, (size_t)find_n, sizeof find_names[0], cmp_names);
        for (int k = 0; k < find_n; k++) {
            char p[PATH_MAX_ + 16];
            if (snprintf(p, sizeof p, "%s%s%s", dir, dir[0] ? "/" : "", find_names[k]) >= (int)sizeof p) continue;
            find_dirs[k] = !vfs_exists(p);
            int64_t s = find_dirs[k] ? 0 : vfs_size(p);
            find_sizes[k] = s < 0 ? 0 : (uint32_t)s;
        }
        find_next();
        break;
    }
    case 0x4f: find_next(); break;
    default: {
        char m[80];
        snprintf(m, sizeof m, "dos: int 21h ah=%02x al=%02x not served", AH, al);
        plat_log(m);
        R.cf = true;
    }
    }
}
