#include "exe.h"
#include "pklite.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PkProgram prog;
static ExeEntry entry;

uint32_t crc32(const void *data, size_t len)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    const uint8_t *p = data;
    uint32_t c = 0xffffffffu;
    while (len--) c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void exe_free(void)
{
    pklite_free(&prog);
    memset(&entry, 0, sizeof entry);
}

bool exe_init(char *err, size_t errcap)
{
    exe_free();
    size_t size = 0;
    uint8_t *file = vfs_read_all(EXE_PATH, &size);
    char why[160] = "";
    if (!file) snprintf(why, sizeof why, "not found (the game program is needed for its code's tables)");
    else if (size != EXE_SIZE || crc32(file, size) != EXE_CRC32)
        snprintf(why, sizeof why, "unsupported version: %zu bytes, CRC-32 %08x (expected the 1994 floppy release: "
                 "28,955 bytes, CRC-32 8e84eb22)", size, (unsigned)crc32(file, size));
    else if (!pklite_unpack(file, size, &prog, why, sizeof why)) {
    } else if (prog.size != EXE_IMAGE_SIZE || crc32(prog.image, prog.size) != EXE_IMAGE_CRC32 || prog.nreloc != EXE_NRELOC)
        snprintf(why, sizeof why, "unpacked image differs: %zu bytes, CRC-32 %08x, %d relocations (expected 82,493 "
                 "bytes, 943cfcf8, 279)", prog.size, (unsigned)crc32(prog.image, prog.size), prog.nreloc);
    free(file);
    if (why[0]) {
        if (err && errcap) snprintf(err, errcap, "%s: %s", EXE_PATH, why);
        exe_free();
        return false;
    }
    entry = (ExeEntry){ (uint16_t)(prog.cs + EXE_LOAD_SEG), prog.ip, (uint16_t)(prog.ss + EXE_LOAD_SEG), prog.sp, prog.nreloc,
                        prog.reloc };
    return true;
}

bool exe_loaded(void) { return prog.image != NULL; }
const ExeEntry *exe_entry(void) { return &entry; }

const uint8_t *exe_image(size_t *size)
{
    if (size) *size = prog.size;
    return prog.image;
}

const uint8_t *exe_data(uint32_t lin, size_t len)
{
    if (!prog.image || lin > prog.size || len > prog.size - lin) return NULL;
    return prog.image + lin;
}

uint8_t exe_u8(uint32_t lin)
{
    const uint8_t *p = exe_data(lin, 1);
    return p ? p[0] : 0;
}

uint16_t exe_u16(uint32_t lin)
{
    const uint8_t *p = exe_data(lin, 2);
    return p ? (uint16_t)(p[0] | p[1] << 8) : 0;
}

uint32_t exe_u32(uint32_t lin)
{
    const uint8_t *p = exe_data(lin, 4);
    return p ? p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}

const char *exe_str(uint32_t lin)
{
    const uint8_t *p = exe_data(lin, 1);
    if (!p) return "";
    return memchr(p, 0, prog.size - lin) ? (const char *)p : "";
}
