"""A minimal DOS loader for Unicorn (exploration and differential checks): loads an MZ program the way
DOS does (PSP at the segment below the load segment, relocations added, SS:SP and CS:IP from the header)
and serves the few int 21h calls a loader stub needs. Nothing here is the game's; the program comes from
the user's copy at run time."""
import struct

import unicorn
from unicorn import x86_const as X

PSP_SEG = 0x0FF0           # the program's PSP; the image loads at PSP_SEG + 0x10
LOAD_SEG = PSP_SEG + 0x10
MEM_TOP = 0xA000           # paragraphs: conventional memory ends at 640 KB


class Dos:
    def __init__(self, exe_bytes, cmdline=b''):
        d = exe_bytes
        (self.last, self.blocks, self.nrel, self.hdr, self.minalloc, self.maxalloc, ss, sp, _csum, ip, cs,
         self.reloff) = struct.unpack_from('<HHHHHHHHHHHH', d, 2)
        size = self.blocks * 512 - (512 - self.last if self.last else 0)
        self.image = d[self.hdr * 16:size]
        self.mu = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_16)
        self.mu.mem_map(0, 0x110000)
        img = bytearray(self.image)
        for i in range(self.nrel):
            off, seg = struct.unpack_from('<HH', d, self.reloff + 4 * i)
            p = seg * 16 + off
            struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + LOAD_SEG) & 0xffff)
        self.mu.mem_write(LOAD_SEG * 16, bytes(img))
        psp = bytearray(256)
        psp[0:2] = b'\xcd\x20'
        struct.pack_into('<H', psp, 2, MEM_TOP)
        psp[0x80] = len(cmdline)
        psp[0x81:0x81 + len(cmdline) + 1] = cmdline + b'\r'
        self.mu.mem_write(PSP_SEG * 16, bytes(psp))
        for r, v in ((X.UC_X86_REG_CS, (cs + LOAD_SEG) & 0xffff), (X.UC_X86_REG_IP, ip),
                     (X.UC_X86_REG_SS, (ss + LOAD_SEG) & 0xffff), (X.UC_X86_REG_SP, sp),
                     (X.UC_X86_REG_DS, PSP_SEG), (X.UC_X86_REG_ES, PSP_SEG)):
            self.mu.reg_write(r, v)
        self.exited = None
        self.mu.hook_add(unicorn.UC_HOOK_INTR, self._intr)

    def reg(self, r):
        return self.mu.reg_read(r)

    def _intr(self, mu, intno, _):
        ah = (self.reg(X.UC_X86_REG_AX) >> 8) & 0xff
        if intno == 0x21 and ah == 0x30:          # DOS version: 5.0
            mu.reg_write(X.UC_X86_REG_AX, 0x0005)
        elif intno == 0x21 and ah == 0x4A:        # resize the program's block: fine
            mu.reg_write(X.UC_X86_REG_FLAGS, self.reg(X.UC_X86_REG_FLAGS) & ~1)
        elif (intno == 0x21 and ah == 0x4C) or intno == 0x20:
            self.exited = self.reg(X.UC_X86_REG_AX) & 0xff
            mu.emu_stop()
        else:
            raise RuntimeError('unhandled int %02xh ah=%02x at %04x:%04x' % (
                intno, ah, self.reg(X.UC_X86_REG_CS), self.reg(X.UC_X86_REG_IP)))
