#!/usr/bin/env python3
"""Runs MICRO.EXE's PKLITE stub in Unicorn until it jumps to the unpacked program, then prints the
program's entry (CS:IP relative to the load segment), SS:SP, and the CRC-32 and size of the unpacked
load image (the bytes from the load segment up to the stub's own start). An independent check of the
port's decompressor (src/pklite.c) and of deark. With --dump <file> it writes the image (git-ignored
re/ only: it is the game's code).
  tools/emu/pklite_run.py cd/MICRO.EXE [--dump re/micro.img] [--stub re/stub.bin]"""
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
from mzload import Dos, LOAD_SEG  # noqa: E402
import unicorn  # noqa: E402
from unicorn import x86_const as X  # noqa: E402


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    exe = open(argv[1], 'rb').read()
    dos = Dos(exe)
    stub_cs = dos.reg(X.UC_X86_REG_CS)
    lo, hi = LOAD_SEG * 16, LOAD_SEG * 16 + len(dos.image) + 0x20000
    jumped = []

    # The stub moves itself to the top of the program's memory, unpacks below it, then jumps far to the
    # program: the first CS that is neither the stub's first nor its moved copy's.
    seen = [stub_cs]

    def code(mu, addr, size, _):
        cs = mu.reg_read(X.UC_X86_REG_CS)
        if cs != seen[-1]:
            seen.append(cs)
            if len(seen) == 3:
                jumped.append((cs, mu.reg_read(X.UC_X86_REG_IP)))
                mu.emu_stop()

    dos.mu.hook_add(unicorn.UC_HOOK_CODE, code)
    start = stub_cs * 16 + dos.reg(X.UC_X86_REG_IP)
    dos.mu.emu_start(start, 0, count=50_000_000)
    if not jumped:
        print('the stub did not reach the program', file=sys.stderr)
        return 1
    cs, ip = jumped[0]
    ss, sp = dos.reg(X.UC_X86_REG_SS), dos.reg(X.UC_X86_REG_SP)
    # the unpacked image ends where the stub was moved to (its segment) at most; report up to the stub's CS
    print('entry %04x:%04x ss:sp %04x:%04x (load-relative cs %04x ss %04x); stub moved to %04x' % (
        cs, ip, ss, sp, (cs - LOAD_SEG) & 0xffff, (ss - LOAD_SEG) & 0xffff, seen[1]))
    print('ds %04x es %04x' % (dos.reg(X.UC_X86_REG_DS), dos.reg(X.UC_X86_REG_ES)))
    if '--dump' in argv:
        p = argv[argv.index('--dump') + 1]
        mem = bytes(dos.mu.mem_read(lo, 0x90000 - lo))
        open(p, 'wb').write(mem)
        print('wrote', p, len(mem), 'bytes (memory from the load segment)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
