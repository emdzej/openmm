#include "mem.h"
#include "cpu.h"
#include "exe.h"
#include <string.h>

uint8_t g_mem[MEM_SIZE] __attribute__((aligned(16)));
Cpu R;
bool g_restart;   /* src/game/game.h */

void mem_write(uint16_t seg, uint16_t off, const void *src, size_t len)
{
    const uint8_t *s = src;
    for (size_t i = 0; i < len; i++) g_mem[mem_lin(seg, (uint16_t)(off + i))] = s[i];
}

void mem_read(uint16_t seg, uint16_t off, void *dst, size_t len)
{
    uint8_t *d = dst;
    for (size_t i = 0; i < len; i++) d[i] = g_mem[mem_lin(seg, (uint16_t)(off + i))];
}

/* The PSP and the environment DOS builds for a program it starts (`name`: the program's file). */
static void make_psp(const char *name)
{
    memset(mem_ptr(SEG_PSP, 0), 0, 256);
    /* int 20h, the memory top, the environment, the command tail (empty), PKLITE's mark (MICRO.EXE's
       stub writes it; DOS leaves the FCB area as it was, zero here) */
    mbw(SEG_PSP, 0, 0xcd), mbw(SEG_PSP, 1, 0x20);
    mww(SEG_PSP, 2, 0xa000);
    mww(SEG_PSP, 0x2c, SEG_ENV);
    mbw(SEG_PSP, 0x80, 0), mbw(SEG_PSP, 0x81, 0x0d);
    static const char env[] = "COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0\1\0C:\\MICROM\\";
    mem_write(SEG_ENV, 0, env, sizeof env - 1);
    size_t k = sizeof env - 1, i = 0;
    for (; name[i]; i++) mbw(SEG_ENV, (uint16_t)(k + i), (uint8_t)name[i]);
    mbw(SEG_ENV, (uint16_t)(k + i), 0);
}

void mem_init_machine(void)
{
    memset(g_mem, 0, sizeof g_mem);
    /* the BIOS data area: the video mode, columns, the CRTC's port (0040:0063, which the game reads to
       find the status register 3DAh), rows; the equipment word (a VGA) */
    mbw(SEG_BIOS, 0x49, 3);
    mww(SEG_BIOS, 0x4a, 80);
    mww(SEG_BIOS, 0x63, 0x3d4);
    mbw(SEG_BIOS, 0x84, 24);
    mww(SEG_BIOS, 0x10, 0x0021);
}

void mem_place_image(const uint8_t *img, size_t n, const uint32_t *reloc, int nreloc, const char *name)
{
    memcpy(g_mem + mem_lin(SEG_CODE, 0), img, n);
    for (int i = 0; i < nreloc; i++) {               /* each word gets the load segment added */
        uint32_t a = mem_lin(SEG_CODE, 0) + reloc[i];
        uint16_t v = (uint16_t)(g_mem[a] | g_mem[a + 1] << 8);
        v = (uint16_t)(v + EXE_LOAD_SEG);
        g_mem[a] = (uint8_t)v, g_mem[a + 1] = (uint8_t)(v >> 8);
    }
    make_psp(name);
}

bool mem_load_program(void)
{
    size_t n;
    const uint8_t *img = exe_image(&n);
    if (!img) return false;
    mem_init_machine();
    const ExeEntry *e = exe_entry();
    mem_place_image(img, n, e->reloc, e->nreloc, "MICRO.EXE");
    mww(SEG_PSP, 0x5c, 0x4b50);
    return true;
}
