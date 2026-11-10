#!/usr/bin/env python3
"""Disassemble 16-bit real-mode code from a DOS MZ program (exploration only; prints to stdout, never
writes files into the tree). Offsets are image offsets (bytes after the MZ header), i.e. linear addresses
with the load segment at 0; `seg:off` is accepted too.
  tools/dis16.py <exe> <start> [count]          e.g. tools/dis16.py re/micro.exe 0x1234 40
  tools/dis16.py <exe> --raw <start> [count]    the file as is (a packed exe's stub, before unpacking)
Needs capstone (the venv in .deps/emu-venv has it)."""
import struct
import sys

import capstone


def parse_addr(s):
    if ':' in s:
        seg, off = s.split(':')
        return int(seg, 16) * 16 + int(off, 16)
    return int(s, 0)


def main(argv):
    raw = '--raw' in argv
    argv = [a for a in argv if a != '--raw']
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    data = open(argv[1], 'rb').read()
    if not raw:
        data = data[struct.unpack_from('<H', data, 8)[0] * 16:]
    start = parse_addr(argv[2])
    count = int(argv[3], 0) if len(argv) > 3 else 60
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    n = 0
    for insn in md.disasm(data[start:start + count * 8], start):
        print('%05x  %-20s %s %s' % (insn.address, insn.bytes.hex(), insn.mnemonic, insn.op_str))
        n += 1
        if n >= count:
            break
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
