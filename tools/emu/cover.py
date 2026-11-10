#!/usr/bin/env python3
"""Code coverage of the unpacked MICRO.EXE in the reference harness (dos.py), for tools/funcmap.py.
Unlike run.py --coverage it records only from the first entry into Start (1000:0006) after the PKLITE
stub has unpacked the program (before that, segment 1000 holds the packed file), and it records the
targets of indirect near calls and jumps as they run. Output (git-ignored out/emu/):
  coverage.txt   seg:off size     executed blocks of segment 1000 (merged with an existing file: --merge)
  indirect.txt   site target n    the observed targets of indirect transfers in segment 1000
  tools/emu/cover.py [--frames 3000] [--keys frame:scancode,...] [--hold 4] [--merge] [--shots N]
--keys: frame:scancode in hex; a press is released --hold frames later unless the code has 0x80 set
(then it is sent as given)."""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import capstone  # noqa: E402
import dos  # noqa: E402
import unicorn  # noqa: E402

BASE = 0x10000
END = BASE + 0x93c0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default='game')
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--keys', default='')
    ap.add_argument('--hold', type=int, default=4)
    ap.add_argument('--merge', action='store_true')
    ap.add_argument('--shots', type=int, default=0)
    ap.add_argument('--out', default='out/emu')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    m = dos.Machine(os.path.join(a.data, 'MICRO.EXE'), a.data, os.path.join(a.out, 'writes'))
    m.skip_card = True
    keys = {}
    for kv in filter(None, a.keys.split(',')):
        f, c = kv.split(':')
        f, c = int(f), int(c, 16)
        keys.setdefault(f, []).append(c)
        if not c & 0x80:
            keys.setdefault(f + a.hold, []).append(c | 0x80)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    st = {'on': False, 'prev': None}
    cov, ind = {}, {}
    sites = {}     # block start -> indirect site offset inside it (last insn of the block)

    def last_insn(addr, size):
        code = m.rd(addr, size)
        last = None
        for i in md.disasm(code, addr - BASE):
            last = i
        return last

    def block(mu, addr, size, _):
        if not st['on']:
            if addr == BASE + 6 and m.rd(addr, 3) == b'\xe8\xe7\x31':
                st['on'] = True
            else:
                return
        prev = st['prev']
        if prev is not None and prev in sites and sites[prev] is not None:
            k = (sites[prev], addr - BASE if BASE <= addr < END else addr)
            ind[k] = ind.get(k, 0) + 1
        if BASE <= addr < END:
            if addr not in cov:
                li = last_insn(addr, size)
                s = None
                if li is not None and li.mnemonic in ('call', 'jmp') and li.bytes[0] == 0xff and \
                        ((li.bytes[1] >> 3) & 7) in (2, 4):
                    s = li.address
                sites[addr] = s
            cov[addr] = max(cov.get(addr, 0), size)
            st['prev'] = addr
        else:
            st['prev'] = None
    m.mu.hook_add(unicorn.UC_HOOK_BLOCK, block)
    per_frame = int(dos.FRAME_TICKS / (dos.SLICE * dos.TICKS_PER_INSN))
    frame = 0
    try:
        for frame in range(a.frames):
            m.key_queue.extend(keys.get(frame, []))
            m.step(per_frame)
            if a.shots and (frame + 1) % a.shots == 0 and m.video_mode in (0x0d, 0x10, 0x12, 0x13):
                dos.write_png(os.path.join(a.out, 'c%05d.png' % (frame + 1)), *m.frame())
    except dos.Exit as e:
        print('exit %d at frame %d' % (e.args[0], frame))
    except RuntimeError as e:
        print('stopped at frame %d: %s' % (frame, e))
    path = os.path.join(a.out, 'coverage.txt')
    old = {}
    if a.merge and os.path.exists(path):
        for line in open(path):
            o, n = line.split()
            old[BASE + int(o[5:], 16)] = int(n)
    for k, v in cov.items():
        old[k] = max(old.get(k, 0), v)
    with open(path, 'w') as f:
        for addr in sorted(old):
            f.write('1000:%04x %d\n' % (addr - BASE, old[addr]))
    ipath = os.path.join(a.out, 'indirect.txt')
    oldi = {}
    if a.merge and os.path.exists(ipath):
        for line in open(ipath):
            s, t, n = line.split()
            oldi[(int(s, 16), int(t, 16))] = int(n)
    for k, v in ind.items():
        oldi[k] = oldi.get(k, 0) + v
    with open(ipath, 'w') as f:
        for (s, t), n in sorted(oldi.items()):
            f.write('%04x %04x %d\n' % (s, t, n))
    print('frames %d, cs:ip %04x:%04x; coverage %d blocks, %d bytes (this run %d); %d indirect edges' % (
        frame + 1, m.r('CS'), m.r('IP'), len(old), sum(old.values()), sum(cov.values()), len(oldi)))


if __name__ == '__main__':
    main()
