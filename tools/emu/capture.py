#!/usr/bin/env python3
"""Records one call of one routine of the original in the reference harness (dos.py), for checking its
port alone (build/mm_fncheck, tools/fncheck.c). The game runs as in run.py until the routine's entry is
reached for the Nth time; from there to its return no interrupt is delivered, and the harness writes
into out/fn/<seg>_<off>_<n>/ (git-ignored: it holds the program's memory):
  before.bin, after.bin   the 1 MB (+64 KB) memory at entry and at return (A000 as the chained view)
  events.txt              the registers at entry (B) and return (F), the video state (V), then in order
                          every port read (I port value) and write (O port value), every int and every
                          far call into the overlay at 2424:0000 the routine made (N n / D, the registers
                          before, A the registers after, M lin hexbytes for the memory it changed, E)
Registers: ax bx cx dx si di bp sp ds es ss flags cs ip, hex.
  tools/emu/capture.py 1000:2efd [--hit 1] [--kind near|far|irq] [--keys ...] [--frames 3000] [--skip-card]
                       [--opaque build/gen/unported.txt | 1000:1234,1000:5678]
--opaque: near calls from the routine (at any depth) to these routines are recorded whole, like the
driver's (C addr, the registers before, A after, M, E), so a routine is checked while its callees are
not ported (their generated stand-ins replay them). Default: build/gen/unported.txt if it exists.
Exits 1 if the routine is not reached, or does not return (it may wait for an interrupt)."""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import dos  # noqa: E402
import unicorn  # noqa: E402

REGS = ('AX', 'BX', 'CX', 'DX', 'SI', 'DI', 'BP', 'SP', 'DS', 'ES', 'SS', 'FLAGS', 'CS', 'IP')
MEM_SIZE = 0x110000
MAX_INSNS = 200_000_000


def snapshot(m):
    lo = m.rd(0, 0xA0000)
    if m.vram_ram:
        vga = m.rd(0xA0000, 0x10000)
    else:
        p = m.vga.planes
        vga = bytes(p[i & 3][i >> 2] for i in range(0x10000))
    hi = m.rd(0xB0000, MEM_SIZE - 0xB0000)
    return lo + vga + hi


def regs(m):
    return ' '.join('%x' % m.r(n) for n in REGS)


def diff(a, b):
    """The changed spans of b against a, as (start, bytes), merged across small gaps."""
    out, i, n, step = [], 0, len(a), 4096
    while i < n:
        if a[i:i + step] == b[i:i + step]:
            i += step
            continue
        j = i
        end = min(i + step, n)
        while j < end:
            if a[j] != b[j]:
                k = j
                while k < end and (a[k] != b[k] or (k + 8 < end and a[k:k + 8] != b[k:k + 8])):
                    k += 1
                if out and out[-1][0] + len(out[-1][1]) >= j - 8:
                    s0 = out[-1][0]
                    out[-1] = (s0, b[s0:k])
                else:
                    out.append((j, b[j:k]))
                j = k
            else:
                j += 1
        i = end
    return out


def video(m):
    return 'V %x %s' % (m.video_mode, bytes(m.dac).hex())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('addr')
    ap.add_argument('--hit', type=int, default=1)
    ap.add_argument('--kind', default='near', choices=('near', 'far', 'irq'))
    ap.add_argument('--exe', default='MICRO.EXE')
    ap.add_argument('--data', default='game')
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--keys', default='')
    ap.add_argument('--code-card', action='store_true')
    ap.add_argument('--skip-card', action='store_true')
    ap.add_argument('--out', default='out/fn')
    ap.add_argument('--opaque', default='build/gen/unported.txt')
    a = ap.parse_args()
    seg, off = (int(x, 16) for x in a.addr.split(':'))
    lin = seg * 16 + off
    m = dos.Machine(os.path.join(a.data, a.exe), a.data, os.path.join(a.out, 'writes'))
    m.code_card, m.skip_card = a.code_card, a.skip_card
    keys = {}
    for kv in filter(None, a.keys.split(',')):
        f, c = kv.split(':')
        keys.setdefault(int(f), []).append(int(c, 16))

    st = {'hits': 0, 'rec': None, 'done': False, 'nested': None, 'int': None}
    events = []
    pop = {'near': 2, 'far': 4, 'irq': 6}[a.kind]

    def on_entry(mu, addr, size, _):
        if st['rec'] is not None or st['done']:
            return
        st['hits'] += 1
        if st['hits'] != a.hit:
            return
        sp = m.r('SP')
        ip, cs = struct.unpack('<HH', m.rd(m.r('SS') * 16 + sp, 4))
        if a.kind == 'near':
            cs = m.r('CS')
        st['rec'] = {'ret': cs * 16 + ip, 'sp': (sp + pop) & 0xffff, 'ss': m.r('SS'),
                     'before': snapshot(m), 'B': regs(m), 'Vb': video(m), 'insns': m.insns}
        m.irq_hold = True
        mu.hook_add(unicorn.UC_HOOK_CODE, on_ret, None, cs * 16 + ip, cs * 16 + ip)

    def on_ret(mu, addr, size, _):
        r = st['rec']
        if st['done'] or r is None or m.r('SP') != r['sp'] or m.r('SS') != r['ss']:
            return
        r['after'], r['F'], r['Va'] = snapshot(m), regs(m), video(m)
        st['done'] = True
        m.irq_hold = False
        mu.emu_stop()

    # far calls into the overlay: recorded whole, the driver's own port accesses inside
    def on_drv(mu, addr, size, _):
        if st['rec'] is None or st['done'] or st['nested']:
            return
        sp = m.r('SP')
        ip, cs = struct.unpack('<HH', m.rd(m.r('SS') * 16 + sp, 4))
        st['nested'] = {'sp': (sp + 4) & 0xffff, 'mem': snapshot(m), 'regs': regs(m), 'tag': 'D'}
        ret_hook(cs * 16 + ip)

    hooked = set()

    def ret_hook(lin_ret):
        if lin_ret not in hooked:
            hooked.add(lin_ret)
            m.mu.hook_add(unicorn.UC_HOOK_CODE, on_drv_ret, None, lin_ret, lin_ret)

    def on_drv_ret(mu, addr, size, _):
        n = st['nested']
        if not n or m.r('SP') != n['sp']:
            return
        events.append('%s %s' % (n['tag'], n['regs']))
        events.append('A %s' % regs(m))
        for s0, b in diff(n['mem'], snapshot(m)):
            events.append('M %x %s' % (s0, b.hex()))
        events.append('E')
        st['nested'] = None

    def on_io(kind, port, v):
        if st['rec'] is not None and not st['done'] and not st['nested'] and st['int'] is None:
            events.append('%s %x %x' % (kind, port, v))

    def on_int(n, phase):
        if st['rec'] is None or st['done'] or st['nested']:
            return
        if phase == 'pre':
            st['int'] = (n, snapshot(m), regs(m))
            return
        n0, mem0, r0 = st['int']
        st['int'] = None
        # the int's own two bytes are past: report the registers at the int as the routine had them
        events.append('N %x %s' % (n0, r0))
        events.append('A %s' % regs(m))
        for s0, b in diff(mem0, snapshot(m)):
            events.append('M %x %s' % (s0, b.hex()))
        events.append('E')

    opaque = set()
    if a.opaque and os.path.exists(a.opaque):
        opaque = {l.strip() for l in open(a.opaque) if l.strip()}
    elif a.opaque and ':' in a.opaque:
        opaque = set(a.opaque.split(','))
    opaque.discard(a.addr.lower())
    opaque_lin = {int(x.split(':')[0], 16) * 16 + int(x.split(':')[1], 16): x for x in opaque}

    def on_opaque(mu, addr, size, _):
        if st['rec'] is None or st['done'] or st['nested'] or addr not in opaque_lin:
            return
        sp = m.r('SP')
        ip = struct.unpack('<H', m.rd(m.r('SS') * 16 + sp, 2))[0]
        st['nested'] = {'sp': (sp + 2) & 0xffff, 'mem': snapshot(m), 'regs': regs(m), 'tag': 'C ' + opaque_lin[addr]}
        ret_hook(m.r('CS') * 16 + ip)

    for x in opaque_lin:
        m.mu.hook_add(unicorn.UC_HOOK_CODE, on_opaque, None, x, x)
    m.mu.hook_add(unicorn.UC_HOOK_CODE, on_entry, None, lin, lin)
    m.mu.hook_add(unicorn.UC_HOOK_CODE, on_drv, None, 0x24240, 0x24240)
    m.on_io, m.on_int = on_io, on_int
    per_frame = int(dos.FRAME_TICKS / (dos.SLICE * dos.TICKS_PER_INSN))
    frame = 0
    try:
        while frame < a.frames and not st['done']:
            m.key_queue.extend(keys.get(frame, []))
            m.step(per_frame)
            frame += 1
            if st['rec'] and m.insns - st['rec']['insns'] > MAX_INSNS:
                print('%s: no return within %d instructions (waits for an interrupt?)' % (a.addr, MAX_INSNS))
                return 1
    except dos.Exit as e:
        print('exit %d at frame %d' % (e.args[0], frame))
    if not st['done']:
        print('%s: hit %d of %d wanted, %s' % (a.addr, st['hits'], a.hit,
                                              'no return' if st['rec'] else 'not reached'))
        return 1
    r = st['rec']
    d = os.path.join(a.out, '%04x_%04x_%d' % (seg, off, a.hit))
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'before.bin'), 'wb').write(r['before'])
    open(os.path.join(d, 'after.bin'), 'wb').write(r['after'])
    with open(os.path.join(d, 'events.txt'), 'w') as f:
        f.write('K %s\n' % a.kind)
        f.write('B %s\n%s\n' % (r['B'], r['Vb']))
        f.write('F %s\n%s\n' % (r['F'], r['Va']))
        for e in events:
            f.write(e + '\n')
    print('%s: frame %d, %d instructions, %d events -> %s' % (a.addr, frame, m.insns - r['insns'], len(events), d))
    return 0


if __name__ == '__main__':
    sys.exit(main())
