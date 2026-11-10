#!/usr/bin/env python3
"""Runs the original MICRO.EXE (or SM.EXE) in the reference harness (dos.py) for a number of frames
(70 Hz of virtual time each) and reports what it did. Output goes to git-ignored out/emu/ only: frames
of the original are the game's pictures.
  tools/emu/run.py [--exe MICRO.EXE] [--data game] [--frames 300] [--keys 120:1c,124:9c]
                   [--shots 50] [--coverage] [--log]
--keys: frame:scancode pairs in hex (0x80 set = release); --shots N: a PNG every N frames;
--coverage: the executed blocks of segment 1000 into out/emu/coverage.txt (seg:off start, size)."""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import dos  # noqa: E402
import unicorn  # noqa: E402


def fnv(h, data):
    for c in data:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return h


def picture_hash(m):
    """As tools/run.c: FNV-1a over the 320 x 200 indices (mode 13h, else zeros) then the DAC widened."""
    if m.video_mode == 0x13:
        p = m.planes()
        px = bytes(p[i & 3][i >> 2] for i in range(64000))
    else:
        px = bytes(64000)
    pal = bytes(((c << 2) | (c >> 4)) & 0xff for c in m.dac)
    return fnv(fnv(2166136261, px), pal)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default='MICRO.EXE')
    ap.add_argument('--data', default='game')
    ap.add_argument('--frames', type=int, default=300)
    ap.add_argument('--keys', default='')
    ap.add_argument('--shots', type=int, default=0)
    ap.add_argument('--coverage', action='store_true')
    ap.add_argument('--log', action='store_true')
    ap.add_argument('--code-card', action='store_true', help="answer FONT.BIN's check (reference runs only)")
    ap.add_argument('--skip-card', action='store_true', help="don't run FONT.BIN: its pass at once (reference runs only)")
    ap.add_argument('--out', default='out/emu')
    ap.add_argument('--hashes', action='store_true',
                    help="each frame's picture hashed as build/mm_run --hashes does (mode 13h: indices, DAC)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    logs = []
    log = (lambda m: (logs.append(m), print(m))) if a.log else logs.append
    m = dos.Machine(os.path.join(a.data, a.exe), a.data, os.path.join(a.out, 'writes'), log=log)
    m.code_card = a.code_card
    m.skip_card = a.skip_card
    keys = {}
    for kv in filter(None, a.keys.split(',')):
        f, c = kv.split(':')
        keys.setdefault(int(f), []).append(int(c, 16))
    cov = {}
    if a.coverage:
        def block(mu, addr, size, _):
            if 0x10000 <= addr < 0x10000 + 0x93c0:
                cov[addr] = max(cov.get(addr, 0), size)
        m.mu.hook_add(unicorn.UC_HOOK_BLOCK, block)
    per_frame = int(dos.FRAME_TICKS / (dos.SLICE * dos.TICKS_PER_INSN))
    frame = 0
    try:
        for frame in range(a.frames):
            m.key_queue.extend(keys.get(frame, []))
            m.step(per_frame)
            if a.hashes:
                print('%d %08x' % (frame, picture_hash(m)), flush=True)
            if a.shots and (frame + 1) % a.shots == 0 and m.video_mode in (0x0d, 0x10, 0x12, 0x13):
                dos.write_png(os.path.join(a.out, 'f%05d.png' % (frame + 1)), *m.frame())
    except dos.Exit as e:
        print('exit %d at frame %d' % (e.args[0], frame))
    except RuntimeError as e:
        print('stopped at frame %d: %s' % (frame, e))
    print('frames %d, %d instructions, cs:ip %04x:%04x, mode %02xh, pit divisor %d, opl writes %d' % (
        frame + 1, m.insns, m.r('CS'), m.r('IP'), m.video_mode, m.pit_period(), len(m.opl_writes)))
    print('ports:', ' '.join('%s %x:%d' % (k[0], k[1], v) for k, v in sorted(m.ports_seen.items(), key=lambda kv: kv[0][1])))
    open(os.path.join(a.out, 'dos.log'), 'w').write('\n'.join(logs) + '\n')
    if a.coverage:
        with open(os.path.join(a.out, 'coverage.txt'), 'w') as f:
            for addr in sorted(cov):
                f.write('1000:%04x %d\n' % (addr - 0x10000, cov[addr]))
        print('coverage: %d blocks, %d bytes' % (len(cov), sum(cov.values())))


if __name__ == '__main__':
    main()
