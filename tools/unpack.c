/* Writes MICRO.EXE unpacked by the port's PKLITE stub (src/pklite.c) as a plain MZ program, for Ghidra
   and the emulator tools. The output is the game's code: write it only into git-ignored re/.
     build/mm_unpack [out.exe]        (default re/micro.exe; data from OPENMM_DATA, default ./game) */
#include "exe.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "re/micro.exe";
    char err[256];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        fprintf(stderr, "mm_unpack: %s\n", vfs_mounted() ? err : "no game data (OPENMM_DATA)");
        return 1;
    }
    size_t n;
    const uint8_t *img = exe_image(&n);
    const ExeEntry *e = exe_entry();
    size_t rel_bytes = (size_t)e->nreloc * 4, hdr = (0x1c + rel_bytes + 15) / 16 * 16;
    uint8_t h[4096] = {0};
    if (hdr > sizeof h) return 1;
    size_t total = hdr + n;
    uint32_t top = (uint32_t)(e->ss - EXE_LOAD_SEG) * 16 + e->sp;   /* the memory the program needs: up to its stack's top */
    unsigned minalloc = top > n ? (unsigned)((top - n + 15) / 16) : 0;
    h[0] = 'M', h[1] = 'Z';
    put16(h + 2, (unsigned)(total % 512));
    put16(h + 4, (unsigned)((total + 511) / 512));
    put16(h + 6, (unsigned)e->nreloc);
    put16(h + 8, (unsigned)(hdr / 16));
    put16(h + 10, minalloc);
    put16(h + 12, 0xffff);
    put16(h + 14, (unsigned)(e->ss - EXE_LOAD_SEG)), put16(h + 16, e->sp);
    put16(h + 20, e->ip), put16(h + 22, (unsigned)(e->cs - EXE_LOAD_SEG));
    put16(h + 24, 0x1c);
    for (int i = 0; i < e->nreloc; i++) {   /* seg:off of each word; all in the first 64 KB group here */
        uint32_t a = e->reloc[i];
        put16(h + 0x1c + 4 * i, a & 0xf), put16(h + 0x1e + 4 * i, a >> 4);
    }
    FILE *f = fopen(out, "wb");
    if (!f || fwrite(h, 1, hdr, f) != hdr || fwrite(img, 1, n, f) != n) {
        fprintf(stderr, "mm_unpack: cannot write %s\n", out);
        return 1;
    }
    fclose(f);
    printf("%s: %zu bytes (image %zu, CRC-32 %08x)\n", out, total, n, (unsigned)crc32(img, n));
    return 0;
}
