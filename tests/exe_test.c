/* The exe as the port reads it (exe.h): the user's MICRO.EXE found through the file layer, its PKLITE stub
   run by the port (pklite.c) and the unpacked image checked against the size and CRC that deark and the
   stub itself, run in an emulator (tools/emu/pklite_run.py), both give. Then the entry state the stub
   hands over, and two of the program's file names read at run time, checked by length and CRC so the
   text stays out of the tree. Skips (exit 0) without data. */
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static bool str_is(uint32_t lin, size_t len, uint32_t crc)
{
    const char *s = exe_str(lin);
    return strlen(s) == len && crc32(s, len) == crc;
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) {
        printf("exe_test: no MICRO.EXE, skipped\n");
        return 0;
    }
    if (!exe_init(err, sizeof err)) {
        printf("FAIL %s\n", err);
        return 1;
    }
    size_t n = 0;
    const uint8_t *img = exe_image(&n);
    CHECK(img && n == EXE_IMAGE_SIZE && crc32(img, n) == EXE_IMAGE_CRC32);
    const ExeEntry *e = exe_entry();
    CHECK(e->cs == 0x1000 && e->ip == 0x0006);        /* the program's entry */
    CHECK(e->ss == 0x8d78 && e->sp == 0x0400);        /* its stack, far above the image (about 503 KB in) */
    CHECK(e->nreloc == EXE_NRELOC);
    int inside = 0;
    for (int i = 0; i < e->nreloc; i++) inside += e->reloc[i] + 2 <= n;
    CHECK(inside == e->nreloc);
    CHECK(str_is(EXE_LIN(0x1a56, 0x0000), 11, 0x79b3a0b8));   /* the first round's map name */
    CHECK(str_is(EXE_LIN(0x1a58, 0x0002), 10, 0x1bcc210c));   /* the cheats' file name */
    CHECK(exe_data(EXE_IMAGE_SIZE, 1) == NULL && exe_data(EXE_IMAGE_SIZE - 1, 1) != NULL);
    printf("MICRO.EXE: unpacked %zu bytes, CRC-32 %08x, entry %04x:%04x, stack %04x:%04x, %d relocations\n", n,
           (unsigned)crc32(img, n), e->cs, e->ip, e->ss, e->sp, e->nreloc);
    exe_free();
    printf("exe_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
