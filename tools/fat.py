#!/usr/bin/env python3
"""An independent reader of FAT12/FAT16 floppy images (no code shared with src/vfs.c), for checking the
port's file layer.
  tools/fat.py list <image>                files with size and CRC-32
  tools/fat.py --summary <image>           the CRC-32 of the lines "PATH SIZE CRC\\n" (paths upper case
                                           with '/', sorted): vfs_test's OPENMM_FAT_SUMMARY
  tools/fat.py extract <image> <dir>       copies the files out (into a git-ignored folder: game/)
"""
import os
import struct
import sys
import zlib


class Fat:
    def __init__(self, data):
        self.d = data
        (bps, spc, res, nfats, roots, total, _media, spf) = struct.unpack_from('<HBHBHHBH', data, 11)
        if not total:
            total = struct.unpack_from('<I', data, 32)[0]
        self.bps, self.spc = bps, spc
        fat = res * bps
        self.fat = data[fat:fat + spf * bps]
        self.root = (res + nfats * spf) * bps
        self.roots = roots
        self.data = self.root + (roots * 32 + bps - 1) // bps * bps
        self.ncl = (total - self.data // bps) // spc
        self.fat16 = self.ncl >= 4085

    def next(self, c):
        if self.fat16:
            return struct.unpack_from('<H', self.fat, 2 * c)[0]
        v = struct.unpack_from('<H', self.fat, c * 3 // 2)[0]
        return v >> 4 if c & 1 else v & 0xfff

    def chain(self, c):
        end = 0xfff8 if self.fat16 else 0xff8
        out = []
        while 2 <= c < end and len(out) <= self.ncl:
            out.append(c)
            c = self.next(c)
        return out

    def clusters(self, c):
        size = self.bps * self.spc
        return b''.join(self.d[self.data + (k - 2) * size:self.data + (k - 1) * size] for k in self.chain(c))

    def entries(self, raw, prefix):
        for p in range(0, len(raw) - 31, 32):
            r = raw[p:p + 32]
            if r[0] == 0:
                return
            if r[0] in (0xe5, ord('.')) or r[11] == 0x0f or r[11] & 0x08:
                continue
            name = r[0:8].decode('latin-1').rstrip()
            ext = r[8:11].decode('latin-1').rstrip()
            full = prefix + name + ('.' + ext if ext else '')
            cl, size = struct.unpack_from('<HI', r, 26)
            if r[11] & 0x10:
                yield from self.entries(self.clusters(cl), full + '/')
            else:
                yield full, self.clusters(cl)[:size]

    def files(self):
        return self.entries(self.d[self.root:self.root + self.roots * 32], '')


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, img = argv[1], argv[2]
    fat = Fat(open(img, 'rb').read())
    files = sorted((p.upper(), b) for p, b in fat.files())
    if cmd == 'list':
        for p, b in files:
            print('%-24s %7d %08x' % (p, len(b), zlib.crc32(b)))
    elif cmd == '--summary':
        lines = ''.join('%s %d %08x\n' % (p, len(b), zlib.crc32(b)) for p, b in files)
        print('%08x' % zlib.crc32(lines.encode()))
    elif cmd == 'extract' and len(argv) > 3:
        for p, b in files:
            out = os.path.join(argv[3], *p.split('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, 'wb').write(b)
        print(len(files), 'files')
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
