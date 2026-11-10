#!/usr/bin/env python3
"""Disassemble 16-bit real-mode code from a DOS MZ program (exploration only; prints to stdout, never
writes files into the tree). Addresses are seg:off with the image at segment 1000h (src/exe.h), or a plain
image offset (bytes after the MZ header). Output is in seg:off of the code segment given (default 1000).
  tools/dis16.py <exe> <start> [count]          e.g. tools/dis16.py re/micro.exe 1000:0100 40
  tools/dis16.py <exe> --raw <start> [count]    the file as is (a packed exe's stub, before unpacking)
Needs capstone (the venv in .deps/emu-venv has it)."""
import struct
import sys

import capstone


def parse_addr(s):
    if ':' in s:
        seg, off = s.split(':')
        return (int(seg, 16) - 0x1000) * 16 + int(off, 16), int(seg, 16)
    return int(s, 0), None


def main(argv):
    raw = '--raw' in argv
    argv = [a for a in argv if a != '--raw']
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    data = open(argv[1], 'rb').read()
    if not raw:
        data = data[struct.unpack_from('<H', data, 8)[0] * 16:]
    start, seg = parse_addr(argv[2])
    count = int(argv[3], 0) if len(argv) > 3 else 60
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    n = 0
    base = (seg - 0x1000) * 16 if seg is not None and not raw else 0
    for insn in md.disasm(data[start:start + count * 8], start - base):
        loc = '%04x:%04x' % (seg, insn.address) if seg is not None and not raw else '%05x' % insn.address
        print('%s  %-20s %s %s' % (loc, insn.bytes.hex(), insn.mnemonic, insn.op_str))
        n += 1
        if n >= count:
            break
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
