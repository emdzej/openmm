/* The real-mode memory (mem.h): the program loaded at 1000:0000 and relocated the way DOS does it, checked
   at Start's segment loads (DS, SS and the far call into the overlay take their segments from relocated
   words), and the PSP and BIOS data the game reads. Skips (exit 0) without data. */
#include "exe.h"
#include "mem.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) {
        printf("mem_test: no MICRO.EXE, skipped\n");
        return 0;
    }
    if (!exe_init(err, sizeof err)) {
        printf("FAIL %s\n", err);
        return 1;
    }
    CHECK(mem_load_program());
    CHECK(mw(SEG_CODE, 0x0011) == SEG_DATA);    /* Start 1000:0010: mov ax, data segment */
    CHECK(mw(SEG_CODE, 0x0016) == SEG_STACK);   /* 1000:0015: mov ax, stack segment */
    CHECK(mw(SEG_CODE, 0x0037) == SEG_OVL);     /* 1000:0034: call far overlay:0000 (the sound driver) */
    CHECK(mw(SEG_PSP, 0x5c) == 0x4b50 && mw(SEG_PSP, 2) == 0xa000);
    CHECK(mw(SEG_BIOS, 0x63) + 6 == 0x3da);     /* where the game finds the status register */
    /* 64 KB wrap within a segment, as on the CPU */
    mww(SEG_DATA, 0xffff, 0x1234);
    CHECK(mb(SEG_DATA, 0xffff) == 0x34 && mb(SEG_DATA, 0) == 0x12);
    exe_free();
    printf("mem_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
