/* The file layer over the user's data (vfs.h): the folder (OPENMM_DATA, default ./game) and the floppy
   image (OPENMM_IMAGE) must give the same files, byte for byte. Both are summarised the way
   tools/fat.py --summary summarises the image with its own reader: the CRC-32 of the lines "PATH SIZE
   CRC\n" (paths upper case with '/', sorted). With OPENMM_FAT_SUMMARY set (the script's output), the
   image's summary must equal it. Skips (exit 0) without data; the image half skips without an image. */
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

typedef struct { char path[128]; uint32_t size, crc; } Item;
typedef struct { Item *it; int n, cap; char dir[128]; } Walk;

static void walk_dir(Walk *w, const char *dir);

static void on_entry(const char *name, bool is_dir, void *user)
{
    Walk *w = user;
    char path[128];
    if (snprintf(path, sizeof path, "%s%s%s", w->dir, w->dir[0] ? "/" : "", name) >= (int)sizeof path) {
        printf("FAIL path too long: %s/%s\n", w->dir, name);
        fails++;
        return;
    }
    for (char *p = path; *p; p++) *p = (char)toupper((unsigned char)*p);
    if (is_dir) {
        Walk sub = *w;
        memcpy(sub.dir, path, sizeof sub.dir);   /* the same size */
        walk_dir(&sub, path);
        w->it = sub.it, w->n = sub.n, w->cap = sub.cap;
        return;
    }
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 256;
        w->it = realloc(w->it, (size_t)w->cap * sizeof *w->it);
    }
    Item *it = &w->it[w->n++];
    memcpy(it->path, path, sizeof it->path);
    size_t n = 0;
    uint8_t *d = vfs_read_all(path, &n);
    it->size = d ? (uint32_t)n : 0xffffffffu;
    it->crc = d ? crc32(d, n) : 0;
    free(d);
}

static void walk_dir(Walk *w, const char *dir) { vfs_list(dir, on_entry, w); }

static int by_path(const void *a, const void *b) { return strcmp(((const Item *)a)->path, ((const Item *)b)->path); }

static uint32_t summary(Walk *w)
{
    qsort(w->it, (size_t)w->n, sizeof *w->it, by_path);
    uint32_t c = 0;
    char line[192];
    /* the CRC of the concatenated lines, computed line by line (crc32 has no continuation: concatenate) */
    size_t cap = (size_t)w->n * sizeof line, len = 0;
    char *all = malloc(cap ? cap : 1);
    for (int i = 0; i < w->n; i++) {
        int k = snprintf(line, sizeof line, "%s %u %08x\n", w->it[i].path, (unsigned)w->it[i].size, (unsigned)w->it[i].crc);
        memcpy(all + len, line, (size_t)k);
        len += (size_t)k;
    }
    c = crc32(all, len);
    free(all);
    return c;
}

static bool collect(const char *src, Walk *w)
{
    memset(w, 0, sizeof *w);
    if (!vfs_mount_path(src)) return false;
    walk_dir(w, "");
    return true;
}

int main(void)
{
    const char *dir = getenv("OPENMM_DATA"), *img = getenv("OPENMM_IMAGE"), *want = getenv("OPENMM_FAT_SUMMARY");
    if (!dir || !*dir) dir = "game";
    Walk a, b;
    if (!collect(dir, &a) || !vfs_exists("MICRO.EXE")) {
        printf("vfs_test: no game data, skipped\n");
        return 0;
    }
    uint32_t sa = summary(&a);
    printf("%s: %s, %d files, summary %08x\n", dir, vfs_describe(), a.n, (unsigned)sa);
    /* names: both separators, any case, ".." from a subdirectory (the game opens "..\BITSFILE.PH0" from GAME1) */
    CHECK(vfs_size("game1\\..\\bitsfile.ph0") == vfs_size("BITSFILE.PH0") && vfs_size("BITSFILE.PH0") > 0);
    CHECK(vfs_size("Game1/Round11.Map") == vfs_size("GAME1/ROUND11.MAP"));
    CHECK(!vfs_exists("GAME1") && !vfs_exists("NOSUCH.BIN"));
    if (img && *img) {
        if (!collect(img, &b)) {
            printf("FAIL %s: not a FAT image\n", img);
            fails++;
        } else {
            uint32_t sb = summary(&b);
            printf("%s: %s, %d files, summary %08x\n", img, vfs_describe(), b.n, (unsigned)sb);
            CHECK(a.n == b.n && sa == sb);
            for (int i = 0; i < a.n && i < b.n; i++)
                if (strcmp(a.it[i].path, b.it[i].path) || a.it[i].size != b.it[i].size || a.it[i].crc != b.it[i].crc) {
                    printf("FAIL differs: %s %u %08x / %s %u %08x\n", a.it[i].path, (unsigned)a.it[i].size,
                           (unsigned)a.it[i].crc, b.it[i].path, (unsigned)b.it[i].size, (unsigned)b.it[i].crc);
                    fails++;
                    break;
                }
            if (want && *want) CHECK(strtoul(want, NULL, 16) == sb);
            /* reads at odd offsets across cluster boundaries match the whole file */
            size_t n = 0;
            uint8_t *whole = vfs_read_all("GFX1.GFX", &n), part[3000];
            VfsFile *f = vfs_open("gfx1.gfx");
            CHECK(whole && f && vfs_read_at(f, 1023, part, sizeof part) == (int64_t)sizeof part &&
                  !memcmp(part, whole + 1023, sizeof part));
            CHECK(f && vfs_read_at(f, n - 5, part, 100) == 5 && vfs_read_at(f, n, part, 1) == 0);
            vfs_close(f);
            free(whole);
            free(b.it);
        }
    } else
        printf("vfs_test: no OPENMM_IMAGE, the image half skipped\n");
    free(a.it);
    vfs_unmount();
    printf("vfs_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
