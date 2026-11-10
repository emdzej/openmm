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
Many routines in one run: a comma list or @file of seg:off, --hit 1,2,5; one recording at a time (a
target entered while another records runs inside it). A recording that runs longer than --max-insns is
dropped (the routine waits for an interrupt, or never returns). Ends with one status line per target;
exits 1 if any wanted hit was not recorded."""
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


IRQ_HANDLERS = {'1000:2efd', '1000:489c', '1000:4a94'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('addr', help='seg:off, a comma list, or @file (one seg:off per line)')
    ap.add_argument('--hit', default='1', help='which entries to record: a comma list (1,2,5)')
    ap.add_argument('--kind', default=None, choices=('near', 'far', 'irq'),
                    help='how the routine returns (default: irq for the three handlers, else near)')
    ap.add_argument('--exe', default='MICRO.EXE')
    ap.add_argument('--data', default='game')
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--keys', default='')
    ap.add_argument('--code-card', action='store_true')
    ap.add_argument('--skip-card', action='store_true')
    ap.add_argument('--out', default='out/fn')
    ap.add_argument('--poke-at', default='1000:3039', help='where --poke writes (default Race_Main)')
    ap.add_argument('--poke', default='', help='seg:off=hex,... written at each --poke-at (other tracks)')
    ap.add_argument('--opaque', default='build/gen/unported.txt')
    ap.add_argument('--max-insns', type=int, default=2_000_000,
                    help='a recording longer than this is dropped (the routine waits for an interrupt)')
    a = ap.parse_args()
    if a.addr.startswith('@'):
        targets = [l.split()[0].lower() for l in open(a.addr[1:]) if l.strip()]
    else:
        targets = [x.lower() for x in a.addr.split(',')]
    hits_wanted = {int(h) for h in a.hit.split(',')}
    tlin = {int(t.split(':')[0], 16) * 16 + int(t.split(':')[1], 16): t for t in targets}
    m = dos.Machine(os.path.join(a.data, a.exe), a.data, os.path.join(a.out, 'writes'))
    m.code_card, m.skip_card = a.code_card, a.skip_card
    if a.poke:
        m.add_pokes(a.poke_at, a.poke)
    keys = {}
    for kv in filter(None, a.keys.split(',')):
        f, c = kv.split(':')
        keys.setdefault(int(f), []).append(int(c, 16))

    recs = []          # the recordings in progress, outermost first; each has its own events
    hits = {t: 0 for t in targets}
    left = {t: set(hits_wanted) for t in targets}
    status = {}
    gave_up = set()    # targets that wait for an interrupt: not tried again
    cur_int = {}

    def on_entry(mu, addr, size, _):
        t = tlin.get(addr)
        if t is None or not left[t] or t in gave_up:
            return
        hits[t] += 1
        if hits[t] not in left[t]:
            return
        kind = a.kind or ('irq' if t in IRQ_HANDLERS else 'near')
        pop = {'near': 2, 'far': 4, 'irq': 6}[kind]
        sp = m.r('SP')
        ip, cs = struct.unpack('<HH', m.rd(m.r('SS') * 16 + sp, 4))
        if kind == 'near':
            cs = m.r('CS')
        recs.append({'t': t, 'hit': hits[t], 'kind': kind, 'sp': (sp + pop) & 0xffff, 'ss': m.r('SS'),
                     'before': snapshot(m), 'B': regs(m), 'Vb': video(m), 'insns': m.insns, 'events': [],
                     'nested': None})
        m.irq_hold = True
        ret_hook(cs * 16 + ip)

    def finish(r):
        t = r['t']
        seg, off = (int(x, 16) for x in t.split(':'))
        d = os.path.join(a.out, '%04x_%04x_%d' % (seg, off, r['hit']))
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, 'before.bin'), 'wb').write(r['before'])
        open(os.path.join(d, 'after.bin'), 'wb').write(snapshot(m))
        with open(os.path.join(d, 'events.txt'), 'w') as f:
            f.write('K %s\n' % r['kind'])
            f.write('B %s\n%s\n' % (r['B'], r['Vb']))
            f.write('F %s\n%s\n' % (regs(m), video(m)))
            for e in r['events']:
                f.write(e + '\n')
        left[t].discard(r['hit'])
        status.setdefault(t, []).append('%d ok' % r['hit'])
        print('%s: hit %d, frame %d, %d instructions, %d events -> %s' % (
            t, r['hit'], frame, m.insns - r['insns'], len(r['events']), d), flush=True)

    def open_recs():
        """The recordings that see what happens now (not inside one of their opaque calls)."""
        return [r for r in recs if r['nested'] is None]

    def start_nested(tag, sp_after, lin_ret, skip=None):
        snap = None
        for r in open_recs():
            if r['t'] == skip:
                continue
            snap = snap or snapshot(m)
            r['nested'] = {'sp': sp_after, 'mem': snap, 'regs': regs(m), 'tag': tag}
        ret_hook(lin_ret)

    # far calls into the overlay: recorded whole, the driver's own port accesses inside
    def on_drv(mu, addr, size, _):
        if not open_recs():
            return
        sp = m.r('SP')
        ip, cs = struct.unpack('<HH', m.rd(m.r('SS') * 16 + sp, 4))
        start_nested('D', (sp + 4) & 0xffff, cs * 16 + ip)

    hooked = set()

    def ret_hook(lin_ret):
        if lin_ret not in hooked:
            hooked.add(lin_ret)
            m.mu.hook_add(unicorn.UC_HOOK_CODE, on_ret, None, lin_ret, lin_ret)

    def on_ret(mu, addr, size, _):
        sp, ss = m.r('SP'), m.r('SS')
        snap = None
        for r in recs:
            n = r['nested']
            if n and sp == n['sp']:
                snap = snap or snapshot(m)
                r['events'].append('%s %s' % (n['tag'], n['regs']))
                r['events'].append('A %s' % regs(m))
                for s0, b in diff(n['mem'], snap):
                    r['events'].append('M %x %s' % (s0, b.hex()))
                r['events'].append('E')
                r['nested'] = None
        while recs and sp == recs[-1]['sp'] and ss == recs[-1]['ss'] and recs[-1]['nested'] is None:
            finish(recs.pop())
        if not recs:
            m.irq_hold = False

    def on_io(kind, port, v):
        if cur_int:
            return
        for r in open_recs():
            r['events'].append('%s %x %x' % (kind, port, v))

    def on_int(n, phase):
        if phase == 'pre':
            rs = open_recs()
            if rs:
                cur_int['x'] = (n, snapshot(m), regs(m), rs)
            return
        if not cur_int:
            return
        n0, mem0, r0, rs = cur_int.pop('x')
        snap = snapshot(m)
        ev = ['N %x %s' % (n0, r0), 'A %s' % regs(m)]
        ev += ['M %x %s' % (s0, b.hex()) for s0, b in diff(mem0, snap)]
        ev.append('E')
        for r in rs:
            r['events'].extend(ev)

    opaque = set()
    if a.opaque and os.path.exists(a.opaque):
        opaque = {l.split()[0] for l in open(a.opaque) if l.strip()}
    elif a.opaque and ':' in a.opaque:
        opaque = set(a.opaque.split(','))
    opaque_lin = {int(x.split(':')[0], 16) * 16 + int(x.split(':')[1], 16): x for x in opaque}

    def on_opaque(mu, addr, size, _):
        if addr not in opaque_lin or not open_recs():
            return
        sp = m.r('SP')
        ip = struct.unpack('<H', m.rd(m.r('SS') * 16 + sp, 2))[0]
        start_nested('C ' + opaque_lin[addr], (sp + 2) & 0xffff, m.r('CS') * 16 + ip, skip=opaque_lin[addr])

    for x in set(opaque_lin) | set(tlin):
        if x in opaque_lin:
            m.mu.hook_add(unicorn.UC_HOOK_CODE, on_opaque, None, x, x)
        if x in tlin:
            m.mu.hook_add(unicorn.UC_HOOK_CODE, on_entry, None, x, x)
    m.mu.hook_add(unicorn.UC_HOOK_CODE, on_drv, None, 0x24240, 0x24240)
    m.on_io, m.on_int = on_io, on_int
    per_frame = int(dos.FRAME_TICKS / (dos.SLICE * dos.TICKS_PER_INSN))
    frame = 0
    try:
        while frame < a.frames and any(left.values()):
            m.key_queue.extend(keys.get(frame, []))
            m.step(per_frame)
            frame += 1
            # a recording running too long waits for an interrupt (or never returns: Main_Restart): it
            # and those inside it are dropped, and the target is not tried again
            for r in [r for r in recs if m.insns - r['insns'] > a.max_insns]:
                status.setdefault(r['t'], []).append('%d waits' % r['hit'])
                left[r['t']].discard(r['hit'])
                gave_up.add(r['t'])
                recs.remove(r)
            if not recs:
                m.irq_hold = False
    except dos.Exit as e:
        print('exit %d at frame %d' % (e.args[0], frame))
    for t in targets:
        miss = sorted(left[t])
        if miss or t not in status:
            status.setdefault(t, []).append('not reached: %s (entries seen %d)' % (','.join(map(str, miss)), hits[t]))
        print('%s %s' % (t, '; '.join(status[t])))
    return 0 if not any(left.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
