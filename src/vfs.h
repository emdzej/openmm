/* Read-only file layer (from OpenRF, via OpenBallance, OpenGTA, OpenCMR2 and OpenI76). Every access to
   the game data goes through here, so the data can come from a directory, a floppy image, or a host that
   has no filesystem at all (a gasm runner's named assets).

   Paths are relative to the data root (the floppy's root or the installed game folder: MICRO.EXE,
   GAME1/), use '/' (or '\'), and match case-insensitively: the game names files in any case
   ("ROUND11.MAP" in GAME1, "..\BITSFILE.PH0" from it).

   One source is mounted at a time:
   - a host directory (vfs_host.c: vfs_mount_path(dir)),
   - a FAT12 or FAT16 disk image (vfs_mount_image) read through a VfsSource, i.e. any "read bytes at
     offset" function: vfs_host.c backs it with a file (.img, .ima), a gasm backend with asset_read_at.

   Thread safety: vfs_read_at on an open file may be called from the audio thread while the main thread
   opens and reads other files (sources and backends read positionally, without a shared cursor). Mounting
   and unmounting must not race with anything. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VfsFile VfsFile;

VfsFile *vfs_open(const char *rel);                  /* NULL if missing */
uint64_t vfs_file_size(const VfsFile *f);
/* Up to len bytes at off: returns the count (0 at or after the end), -1 on error. */
int64_t vfs_read_at(VfsFile *f, uint64_t off, void *dst, size_t len);
void vfs_close(VfsFile *f);

bool vfs_exists(const char *rel);                    /* a file (not a directory) */
int64_t vfs_size(const char *rel);                   /* -1 if missing */
/* Whole file, malloc'd (free() it); NULL if missing or unreadable. size may be NULL. */
uint8_t *vfs_read_all(const char *rel, size_t *size);
/* Calls fn for every entry of a directory (names as on the medium, "NAME.EXT"; no "." / ".."), in
   medium order. Returns false if the directory does not exist. */
typedef void (*VfsListFn)(const char *name, bool is_dir, void *user);
bool vfs_list(const char *dir, VfsListFn fn, void *user);

/* What is mounted, for messages ("game", "disk1.img (FAT12, 720 KB, label MICROMASTER)", ...). */
const char *vfs_describe(void);
bool vfs_mounted(void);
void vfs_unmount(void);

/* ---- backends ---- */

/* A mounted file system. open returns a backend handle (NULL = missing) and the file size. */
typedef struct {
    void *ctx;
    void *(*open)(void *ctx, const char *rel, uint64_t *size);
    int64_t (*read_at)(void *ctx, void *file, uint64_t off, void *dst, size_t len);
    void (*close)(void *ctx, void *file);
    bool (*list)(void *ctx, const char *dir, VfsListFn fn, void *user);
    void (*unmount)(void *ctx);
} VfsBackend;
/* Replaces the current mount (the backend is copied). desc is copied too. */
void vfs_mount(const VfsBackend *b, const char *desc);

/* Random-access bytes of a disk image. read_at: up to len bytes at off, count or -1 (see thread safety
   above). close may be NULL. */
typedef struct {
    void *ctx;
    uint64_t size;
    int64_t (*read_at)(void *ctx, uint64_t off, void *dst, size_t len);
    void (*close)(void *ctx);
} VfsSource;
/* Mounts the FAT file system of a disk image (a boot sector with a BIOS parameter block: 360 KB to
   2.88 MB floppies, or a FAT16 hard-disk partition image). On failure returns false (the source is
   closed) and nothing new is mounted. */
bool vfs_mount_image(const VfsSource *src, const char *desc);
