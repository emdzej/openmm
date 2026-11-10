#include "mem.h"
#include "cpu.h"
#include "exe.h"
#include <string.h>

uint8_t g_mem[MEM_SIZE] __attribute__((aligned(16)));
Cpu R;

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

bool mem_load_program(void)
{
    size_t n;
    const uint8_t *img = exe_image(&n);
    if (!img) return false;
    memset(g_mem, 0, sizeof g_mem);
    memcpy(g_mem + mem_lin(SEG_CODE, 0), img, n);
    const ExeEntry *e = exe_entry();
    for (int i = 0; i < e->nreloc; i++) {             /* each word gets the load segment added */
        uint32_t a = mem_lin(SEG_CODE, 0) + e->reloc[i];
        uint16_t v = (uint16_t)(g_mem[a] | g_mem[a + 1] << 8);
        v = (uint16_t)(v + EXE_LOAD_SEG);
        g_mem[a] = (uint8_t)v, g_mem[a + 1] = (uint8_t)(v >> 8);
    }
    /* the PSP: int 20h, the memory top, the environment, the command tail (empty), PKLITE's mark */
    mbw(SEG_PSP, 0, 0xcd), mbw(SEG_PSP, 1, 0x20);
    mww(SEG_PSP, 2, 0xa000);
    mww(SEG_PSP, 0x2c, SEG_ENV);
    mww(SEG_PSP, 0x5c, 0x4b50);
    mbw(SEG_PSP, 0x80, 0), mbw(SEG_PSP, 0x81, 0x0d);
    static const char env[] = "COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0\1\0C:\\MICROM\\MICRO.EXE";
    mem_write(SEG_ENV, 0, env, sizeof env);
    /* the BIOS data area: the video mode, columns, the CRTC's port (0040:0063, which the game reads to
       find the status register 3DAh), rows; the equipment word (a VGA) */
    mbw(SEG_BIOS, 0x49, 3);
    mww(SEG_BIOS, 0x4a, 80);
    mww(SEG_BIOS, 0x63, 0x3d4);
    mbw(SEG_BIOS, 0x84, 24);
    mww(SEG_BIOS, 0x10, 0x0021);
    return true;
}
