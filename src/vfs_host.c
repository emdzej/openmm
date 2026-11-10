/* Host backends of the file layer: a directory (the floppy's files or an installed game; case-insensitive
   lookup, component by component) and disk-image files read with pread (positional, so the audio thread
   can read while the main thread loads). */
#include "vfs_host.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ directory */

typedef struct { char root[1024]; } HostDir;

/* rel resolved under root, each component matched case-insensitively (a missing one is kept as is). */
static void host_resolve(const HostDir *hd, const char *rel, char *out, size_t cap)
{
    snprintf(out, cap, "%s", hd->root);
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", rel);
    for (char *p = tmp; *p; p++) if (*p == '\\') *p = '/';
    char *save = NULL;
    for (char *tok = strtok_r(tmp, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        DIR *d = opendir(out);
        const char *match = tok;
        struct dirent *e;
        while (d && (e = readdir(d)))
            if (!strcasecmp(e->d_name, tok)) { match = e->d_name; break; }
        size_t n = strlen(out);
        snprintf(out + n, cap - n, "/%s", match);
        if (d) closedir(d);
    }
}

static void *hd_open(void *ctx, const char *rel, uint64_t *size)
{
    char path[2048];
    host_resolve(ctx, rel, path, sizeof path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
    *size = (uint64_t)st.st_size;
    return (void *)(intptr_t)(fd + 1);
}

static int64_t fd_read_at(int fd, uint64_t off, void *dst, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t r = pread(fd, (uint8_t *)dst + done, len - done, (off_t)(off + done));
        if (r < 0) return done ? (int64_t)done : -1;
        if (r == 0) break;
        done += (size_t)r;
    }
    return (int64_t)done;
}

static int64_t hd_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    return fd_read_at((int)(intptr_t)file - 1, off, dst, len);
}

static void hd_close(void *ctx, void *file) { close((int)(intptr_t)file - 1); }

static bool hd_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    char path[2048];
    host_resolve(ctx, dir, path, sizeof path);
    DIR *d = opendir(path);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char full[3072];
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        struct stat st;
        fn(e->d_name, stat(full, &st) == 0 && S_ISDIR(st.st_mode), user);
    }
    closedir(d);
    return true;
}

static void hd_unmount(void *ctx) { free(ctx); }

static bool mount_dir(const char *path)
{
    HostDir *hd = malloc(sizeof *hd);
    if (!hd) return false;
    snprintf(hd->root, sizeof hd->root, "%s", path);
    VfsBackend b = { hd, hd_open, hd_read_at, hd_close, hd_list, hd_unmount };
    vfs_mount(&b, path);
    return true;
}

/* ------------------------------------------------------------------ image files */

static int64_t src_read_at(void *ctx, uint64_t off, void *dst, size_t len) { return fd_read_at((int)(intptr_t)ctx - 1, off, dst, len); }
static void src_close(void *ctx) { close((int)(intptr_t)ctx - 1); }

static bool mount_image_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return false; }
    VfsSource s = { (void *)(intptr_t)(fd + 1), (uint64_t)st.st_size, src_read_at, src_close };
    const char *base = strrchr(path, '/');
    return vfs_mount_image(&s, base ? base + 1 : path);
}

static bool mount_source(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) return mount_dir(path);
    return mount_image_file(path);
}

bool vfs_mount_path(const char *path)
{
    return mount_source(path);
}

bool vfs_mount_default(void)
{
    const char *e = getenv("OPENMM_DATA");
    return vfs_mount_path(e && *e ? e : "game");
}
