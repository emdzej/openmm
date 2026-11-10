#!/usr/bin/env python3
"""Records the original's sound in the reference harness (dos.py): every far call into the sound driver
(DRIVERn.BIN at 2424:0000) with its registers, the game's own writes into the driver's segment since the
previous call, the driver's port accesses and its registers at the return; plus every AdLib register
write of the run with its frame. For checking the port's drivers (build/mm_drvcheck replays the calls,
tools/drv_check.sh) and the port's whole runs (build/mm_run --opl-log, tools/emu/oplcmp.py).

The device is chosen by SETTINGS.DAT (word 3, the n of DRIVERn.BIN): the run uses a copy of the data
folder under out/snd/ (symlinks to the user's files, SETTINGS.DAT rewritten), never the folder itself.
  tools/emu/sndlog.py --driver 1 [--frames 600] [--keys 300:1c,304:9c] [--out out/snd/d1]
writes <out>/calls.txt and <out>/opl.txt (git-ignored out/: the driver's memory is the user's data).

calls.txt, one call per block:
  C frame ax bx cx dx si di bp ds es flags      the registers at the entry (hex)
  P off hexbytes                                the driver segment's bytes changed since the last return
  I port value / O port value / N n             the driver's port reads and writes and its ints, in order
  R ax bx cx dx si di bp ds es flags crc32      the registers at the retf; crc32 of the segment's first
                                                <size of the driver file> bytes
Interrupts are held while a call runs (as capture.py does for a recorded routine): in the port a driver
call is never interrupted (machine.h: the timer runs at frame boundaries), so the calls stay in one
sequence. opl.txt: "frame index value" per write to 389h."""
import argparse
import os
import shutil
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
import dos  # noqa: E402
import unicorn  # noqa: E402

OVL = 0x2424 * 16
REGS = ('AX', 'BX', 'CX', 'DX', 'SI', 'DI', 'BP', 'DS', 'ES', 'FLAGS')


def data_copy(src, dst, driver):
    """A folder of symlinks to src's files, SETTINGS.DAT copied with the sound device word set."""
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    for name in os.listdir(src):
        p = os.path.join(src, name)
        if name.upper() == 'SETTINGS.DAT':
            b = bytearray(open(p, 'rb').read())
            struct.pack_into('<H', b, 6, driver)
            open(os.path.join(dst, name), 'wb').write(bytes(b))
        else:
            os.symlink(os.path.abspath(p), os.path.join(dst, name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--driver', type=int, default=1)
    ap.add_argument('--data', default='game')
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--keys', default='')
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    out = a.out or 'out/snd/d%d' % a.driver
    os.makedirs(out, exist_ok=True)
    data = os.path.join('out/snd', 'data_d%d' % a.driver)
    data_copy(a.data, data, a.driver)
    drv_file = open(os.path.join(a.data, 'DRIVER%d.BIN' % a.driver), 'rb').read()
    size = len(drv_file)
    m = dos.Machine(os.path.join(data, 'MICRO.EXE'), data, os.path.join(out, 'writes'))
    m.skip_card = True
    # Unicorn keeps code it translated from 2424:0000 (FONT.BIN, then DRIVER0.BIN) across the harness's
    # own writes of a file read there (int 21h 3Fh writes from the host): drop it after each read, or the
    # stale DRIVER0 ("mov ax, 0; retf") keeps running in place of the driver read over it.
    int21 = m.int21

    def int21_flush(ah):
        int21(ah)
        if ah == 0x3f:
            m.mu.ctl_remove_cache(OVL, OVL + 0x10000)
    m.int21 = int21_flush
    keys = {}
    for kv in filter(None, a.keys.split(',')):
        f, c = kv.split(':')
        keys.setdefault(int(f), []).append(int(c, 16))

    st = {'frame': 0, 'call': None, 'shadow': bytearray(drv_file), 'calls': 0}
    lines, opl = [], []

    def regs():
        return ' '.join('%x' % m.r(n) for n in REGS)

    def is_driver():
        return m.rd(OVL, 4) == drv_file[:4]

    def on_entry(mu, addr, size_, _):
        if st['call'] is not None or not is_driver():
            return
        cur = m.rd(OVL, size)
        sh = st['shadow']
        lines.append('C %d %s' % (st['frame'], regs()))
        i = 0
        while i < size:
            if cur[i] != sh[i]:
                j = i
                while j < size and (cur[j] != sh[j] or cur[j + 1:j + 5] != sh[j + 1:j + 5]):
                    j += 1
                lines.append('P %x %s' % (i, cur[i:j].hex()))
                i = j
            else:
                i += 1
        sp = m.r('SP')
        st['call'] = (sp + 4) & 0xffff      # SP after the retf
        m.irq_hold = True

    def on_retf(mu, addr, size_, _):
        if st['call'] is None:
            return
        # the retf at 2424:0003; SP still holds the far return address
        if (m.r('SP') + 4) & 0xffff != st['call']:
            return
        cur = m.rd(OVL, size)
        lines.append('R %s %08x' % (regs(), zlib.crc32(cur)))
        st['shadow'] = bytearray(cur)
        st['call'] = None
        st['calls'] += 1
        m.irq_hold = False

    def on_io(kind, port, v):
        if kind == 'O' and port == 0x389:
            opl.append('%d %x %x' % (st['frame'], m.opl_idx, v))
        if st['call'] is not None:
            lines.append('%s %x %x' % (kind, port, v))

    def on_int(n, phase):
        if st['call'] is not None and phase == 'pre':
            lines.append('N %x' % n)

    m.mu.hook_add(unicorn.UC_HOOK_CODE, on_entry, None, OVL, OVL)
    m.mu.hook_add(unicorn.UC_HOOK_CODE, on_retf, None, OVL + 3, OVL + 3)
    m.on_io, m.on_int = on_io, on_int
    per_frame = int(dos.FRAME_TICKS / (dos.SLICE * dos.TICKS_PER_INSN))
    try:
        for frame in range(a.frames):
            st['frame'] = frame
            m.key_queue.extend(keys.get(frame, []))
            m.step(per_frame)
    except dos.Exit as e:
        print('exit %d at frame %d' % (e.args[0], st['frame']))
    open(os.path.join(out, 'calls.txt'), 'w').write('\n'.join(lines) + '\n')
    open(os.path.join(out, 'opl.txt'), 'w').write('\n'.join(opl) + '\n')
    print('DRIVER%d: %d frames, %d driver calls, %d OPL writes -> %s' % (
        a.driver, st['frame'] + 1, st['calls'], len(opl), out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
