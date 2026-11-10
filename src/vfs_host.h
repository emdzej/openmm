/* Host-filesystem backends of the file layer (POSIX): directories and disk-image files. Used by the
   tests and tools; a backend without a filesystem (gasm) mounts a VfsSource instead. */
#pragma once
#include "vfs.h"

/* Mounts a data root: a directory (the floppy's files, or the folder the game was installed to) or a
   FAT disk image of the floppy (.img, .ima). False if it can't be mounted; whether it holds the game is
   up to the caller (vfs_exists("MICRO.EXE")). */
bool vfs_mount_path(const char *path);
/* $OPENMM_DATA if set, else ./game (the floppy's files; tests and tools run from the project root). */
bool vfs_mount_default(void);
