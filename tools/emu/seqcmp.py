#!/usr/bin/env python3
"""Whole-run check: compares the pictures of the original (tools/emu/run.py --hashes) with the port's
(build/mm_run --param splash=0 --hashes) on the same key script. Frames do not line up exactly (the
harness's clock drifts against its frames, loads take emulated time, it can show the screen mid-copy),
so the check is on the sequence of distinct pictures: how many of the original's appear in the port in
the same order, and where they part.
  tools/emu/seqcmp.py out/emu.txt out/port.txt [--all]"""
import difflib
import sys


def seq(path):
    s = []
    for line in open(path):
        t = line.split()
        if len(t) == 2 and t[0].isdigit() and len(t[1]) == 8 and (not s or s[-1][1] != t[1]):
            s.append((int(t[0]), t[1]))
    return s


def main():
    a, b = seq(sys.argv[1]), seq(sys.argv[2])
    m = difflib.SequenceMatcher(None, [x[1] for x in a], [x[1] for x in b], autojunk=False)
    common = sum(x.size for x in m.get_matching_blocks())
    print('distinct pictures: original %d, port %d, in common in order %d' % (len(a), len(b), common))
    ops = [o for o in m.get_opcodes() if o[0] != 'equal']
    for op, i1, i2, j1, j2 in ops if '--all' in sys.argv else ops[:20]:
        print('%-7s original frames %s (%d), port frames %s (%d)' % (
            op, [x[0] for x in a[i1:i2]][:6], i2 - i1, [x[0] for x in b[j1:j2]][:6], j2 - j1))


if __name__ == '__main__':
    main()
