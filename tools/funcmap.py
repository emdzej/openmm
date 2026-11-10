#!/usr/bin/env python3
"""A map of MICRO.EXE's code segment 1000 by recursive-descent disassembly (exploration only: prints to
stdout, or writes into the git-ignored re/ with --out). Addresses are seg:off with the image at 1000h.

  tools/funcmap.py [re/micro.exe] [--coverage out/emu/cov/coverage.txt] [--ghidra re/micro.exe.funcs.txt]
                   [--names tools/ghidra/names.tsv] [--out re/funcmap.txt] [--list] [--gaps] [--tables]
                   [--routine 1000:xxxx] [--tsv] [--modules] [--listing re/funcmap.lst]

The coverage comes from tools/emu/cover.py, which records only after the PKLITE stub has unpacked the
program (run.py --coverage also records the packed stub's blocks in segment 1000, which are not this
program's code). No mode needs it: it only confirms that the static walk misses nothing.

The walk starts at Start (1000:0006) and the seeds below: Ghidra's functions, the harness's executed
blocks (code discovery only, not entries), interrupt handlers the program installs, and the pointer
tables read by hand from the code (TABLES; each names the site that uses it). It follows near jumps,
conditional jumps, loops and near calls; a near call's target is a routine entry. Indirect transfers are
listed as sites; the resolved ones continue into their table's targets.

A routine is an entry and what it reaches through jumps without crossing another entry: a jump to
another entry is a tail jump, running off the end into another entry a fall-through. Code reached from
two entries is shared (multiple entry points). Bytes no instruction covers are the "unreached"
remainder; REGIONS explains each (cs: variables, padding, tables, dead code).
Needs capstone (the venv the harness uses has it)."""
import argparse
import bisect
import os
import struct
import sys

import capstone
from capstone import x86

CODE_END = 0x93c0          # the data segment 193c starts at image offset 93c0h

# Routines reached only through pointers (tables, vectors, stored callbacks): seg 1000 offsets.
EXTRA_ENTRIES = {
    0x489c: 'int 08h handler (installed at 1000:4950)',
    0x2efd: 'int 09h handler (installed at 1000:49a4)',
    0x4a94: 'int 0Ah handler (installed at 1000:4a3f)',
    0x2ded: 'input slot: none, control type 6 = the computer (stored at 1000:2d49)',
    0x2dfa: 'input slot: keys 1, control type 4 (stored at 1000:2d12)',
    0x2dfe: 'input slot: keys 2, control type 5 (stored at 1000:2d1c)',
    0x2e02: 'input slot: mouse, control type 3 (stored at 1000:2d26)',
    0x2e6c: 'input slot: joystick A, control type 1 (stored at 1000:2d35)',
    0x2eb3: 'input slot: joystick B, control type 2 (stored at 1000:2d44)',
}

# Code that nothing reaches (no call, jump, table or stored pointer; checked by a search of the image for
# the address as a word): left-overs, listed as routines so that they are named and explained.
DEAD_ENTRIES = [0x0f70, 0x2099, 0x2706, 0x2736, 0x3171, 0x317f, 0x3192, 0x3a44, 0x3a8d, 0x864f, 0x8929, 0x8b8e, 0x8d9e]

# Pointer tables resolved by reading the code: (table location, entry count, the site(s) that use it, kind)
#   loc: ('ds', off) for the data segment 193c, ('cs', off) for the code segment
#   kind 'call': the targets are routines; 'jump': the targets continue the site's routine
TABLES = [
    # 193c:26d7 holds one pointer per theme (193c:28bf = 1..9, stored into 193c:28bd at 1000:3be6) to a
    # table of terrain handlers indexed by the tile's top bits (>> 5 for themes 1, 2, 3 and 6, else >> 4);
    # the nine tables overlap in the data segment.
    dict(loc=('ds', 0x26e9), count=8, sites=[0x5eea], kind='call', what='terrain handlers, theme 1'),
    dict(loc=('ds', 0x26f9), count=8, sites=[0x5eea], kind='call', what='terrain handlers, theme 2'),
    dict(loc=('ds', 0x2707), count=8, sites=[0x5eea], kind='call', what='terrain handlers, theme 3'),
    dict(loc=('ds', 0x2717), count=16, sites=[0x5eea], kind='call', what='terrain handlers, theme 4'),
    dict(loc=('ds', 0x2735), count=16, sites=[0x5eea], kind='call', what='terrain handlers, theme 5'),
    dict(loc=('ds', 0x274f), count=8, sites=[0x5eea], kind='call', what='terrain handlers, theme 6'),
    dict(loc=('ds', 0x2759), count=16, sites=[0x5eea], kind='call', what='terrain handlers, theme 7'),
    dict(loc=('ds', 0x2779), count=16, sites=[0x5eea], kind='call', what='terrain handlers, theme 8'),
    dict(loc=('ds', 0x277f), count=16, sites=[0x5eea], kind='call', what='terrain handlers, theme 9'),
    # 193c:278f: one handler per vehicle state (word [bx+12ae] of the vehicle record), called at 1000:7d45
    dict(loc=('ds', 0x278f), count=17, sites=[0x7d45], kind='call', what='vehicle state handlers'),
]

# Indirect sites whose targets are known some other way: near calls through a variable that the code
# fills with immediates (CALL_SITE_TARGETS), jumps computed from a register (SITE_TARGETS).
INPUT_SLOTS = [0x2ded, 0x2dfa, 0x2dfe, 0x2e02, 0x2e6c, 0x2eb3]   # 193c:1083..1089, filled at 1000:2d00
CALL_SITE_TARGETS = {0x2dad: INPUT_SLOTS, 0x2db8: INPUT_SLOTS, 0x2dc3: INPUT_SLOTS, 0x2dce: INPUT_SLOTS}
SITE_TARGETS = {
}

# Code-segment bytes that are not code, read by hand: (start, end exclusive, what)
REGIONS = [
    (0x0000, 0x0006, 'zero bytes before Start (the image begins here; nothing reads them)'),
    (0x00f3, 0x0100, 'zero padding: the next routine is aligned at 0100'),
    (0x0213, 0x0220, 'zero padding before 0220'),
    (0x045b, 0x0460, 'zero padding'),
    (0x0460, 0x0461, 'a lone ret (dead)'),
    (0x0461, 0x0470, 'zero padding'),
    (0x070e, 0x0710, 'zero padding'),
    (0x0793, 0x07a0, 'zero padding'),
    (0x08e5, 0x08f0, 'zero padding'),
    (0x0f6d, 0x0f70, 'zero padding before the dead save routine at 0f70'),
    (0x1e18, 0x1e20, 'zero padding'),
    (0x2408, 0x2409, 'a second ret after the routine before (dead)'),
    (0x2768, 0x2770, 'zero padding'),
    (0x296d, 0x296e, 'nop (alignment)'),
    (0x2f99, 0x2f9d, 'cs: the keyboard handler\'s E0-prefix flag (2f99, at 2f1d, 2f2f, 2f37) and three unused zero bytes'),
    (0x2f9d, 0x2f9e, 'a lone ret (dead)'),
    (0x3279, 0x327a, 'cs: byte: the sound driver is loaded (set at 322a/3271, read by the timer handler at 48f1)'),
    (0x3544, 0x3547, 'cs: the decoder keeps its control byte (3544) and the flags (3545, word) here across each code (the two xchg of 3533)'),
    (0x4a8d, 0x4a94, 'cs: the CRTC register 11h as found (4a8d), the previous int 0Ah vector (4a8e, far), the IRQ 2 works flag (4a92, word: set by the int 0Ah handler, tested by the timer handler at 48a4)'),
    (0x4ade, 0x4adf, 'cs: byte: the frame tick counter (incremented by the timer handler at 48cb or the int 0Ah handler at 4ac5; waited on at 3165 and by the race pacing at 30c7)'),
    (0x7a94, 0x7a97, 'zero padding'),
    (0x7af5, 0x7af8, 'cs: variables of 7b46..7c5b: 7af5 a byte counter 40h..43h, 7af6 a word'),
    (0x7cdb, 0x7cdf, 'cs: three bytes read and written as the overlapping words 7cdb, 7cdc, 7cdd by 7cae (a random-number state)'),
    (0x7cdf, 0x7ce0, 'a lone ret (dead)'),
    (0x8711, 0x8712, 'a second ret (dead)'),
    (0x8780, 0x8796, 'cs: word table, 11 entries (0 1 2 3 4 5 4 3 2 1 0): the frame sequence of the shell burst, indexed by [bx+13a4] - 1Eh at 875d'),
    (0x8992, 0x8994, 'popa, ret: the tail of a dead routine'),
    (0x8994, 0x8996, 'cs: word: the low two bits of the screen offset (written at 916a, read by the blits)'),
    (0x8d6f, 0x8d70, 'pusha skipped by the jmp at 8d6d (dead byte)'),
    (0x90c3, 0x90c5, 'cs: word: SP saved by the frame blit at 9165 (compared at 920b to unwind its pushes)'),
    (0x92eb, 0x92f0, 'zero padding'),
    (0x93bf, 0x93c0, 'zero padding to the data segment at 93c0'),
]

# Writes into the code segment that land on instructions (self-modifying code), read by hand:
SELF_MODIFYING = [
    (0x7a75, 0x91a4, 'four stosw of 66 a5 over eight movsw: the row copy becomes four movsd'),
    (0x7a7c, 0x92d9, 'the count of mov cx at 92d8: 80h words become 40h dwords'),
    (0x7a83, 0x92dc, '66 a5 over movsw, nop: rep movsw becomes rep movsd'),
]

# Offsets the walk must not decode as code (data that a seed or a call would otherwise lead into).
NOT_CODE = set()


def load_image(path):
    data = open(path, 'rb').read()
    hdr = struct.unpack_from('<H', data, 8)[0] * 16
    return data[hdr:]


class Map:
    def __init__(self, img):
        self.img = img
        self.code = img[:CODE_END]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
        self.md.detail = True
        self.insn = {}          # off -> capstone insn
        self.entries = {}       # off -> set of reasons
        self.jump_targets = {}  # site -> [targets]
        self.call_sites = {}    # site -> callee
        self.indirect = {}      # site -> text
        self.far = {}           # site -> text (far calls / jumps)
        self.errors = []
        self.cs_refs = {}       # cs: memory operand offsets -> [sites]
        self.ds_tables = {}

    def u16(self, off):
        return struct.unpack_from('<H', self.img, off)[0]

    def table_targets(self, loc, count):
        base = (0x93c0 if loc[0] == 'ds' else 0) + loc[1]
        return [self.u16(base + 2 * i) for i in range(count)]

    def decode(self, off):
        if off in self.insn:
            return self.insn[off]
        if off < 0 or off >= CODE_END or off in NOT_CODE:
            return None
        ins = next(self.md.disasm(self.code[off:off + 16], off), None)
        if ins is None:
            self.errors.append((off, 'undecodable'))
            return None
        self.insn[off] = ins
        return ins

    @staticmethod
    def imm_target(ins):
        if ins.operands and ins.operands[0].type == x86.X86_OP_IMM:
            return ins.operands[0].imm & 0xffff
        return None

    def flow(self, ins):
        """(successors within the routine, call target or None, stops)"""
        g = set(ins.groups)
        m = ins.mnemonic
        nxt = ins.address + ins.size
        if x86.X86_GRP_RET in g or x86.X86_GRP_IRET in g or m in ('ret', 'retf', 'iret', 'hlt'):
            return [], None
        if m == 'jmp' or m == 'ljmp':
            if m == 'ljmp' or ins.bytes[0] in (0xea,) or (ins.bytes[0] == 0xff and ((ins.bytes[1] >> 3) & 7) == 5):
                self.far[ins.address] = '%s %s' % (m, ins.op_str)
                return [], None
            t = self.imm_target(ins)
            if t is None:
                self.indirect[ins.address] = '%s %s' % (m, ins.op_str)
                return list(self.jump_targets.get(ins.address, [])), None
            return [t], None
        if x86.X86_GRP_JUMP in g:       # jcc, loop, jcxz
            return [self.imm_target(ins), nxt], None
        if m in ('call', 'lcall'):
            if m == 'lcall' or ins.bytes[0] == 0x9a or (ins.bytes[0] == 0xff and ((ins.bytes[1] >> 3) & 7) == 3):
                self.far[ins.address] = '%s %s' % (m, ins.op_str)
                return [nxt], None
            t = self.imm_target(ins)
            if t is None:
                self.indirect[ins.address] = '%s %s' % (m, ins.op_str)
                return [nxt], ('indirect', ins.address)
            return [nxt], t
        return [nxt], None

    def note_mem(self, ins):
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM and op.mem.segment == x86.X86_REG_CS:
                self.cs_refs.setdefault(op.mem.disp & 0xffff, []).append(ins.address)

    def walk(self, seeds):
        work = list(seeds)
        seen = set()
        while work:
            off = work.pop()
            if off in seen:
                continue
            seen.add(off)
            ins = self.decode(off)
            if ins is None:
                continue
            self.note_mem(ins)
            succ, callee = self.flow(ins)
            if isinstance(callee, int):
                self.call_sites[ins.address] = callee
                self.entries.setdefault(callee, set()).add('call')
                work.append(callee)
            elif callee and callee[0] == 'indirect':
                for t in self.call_tables.get(ins.address, []):
                    self.entries.setdefault(t, set()).add('table')
                    work.append(t)
            for s in succ:
                if s is not None:
                    work.append(s)
        return seen

    def overlaps(self):
        """Instructions whose bytes overlap another decoded instruction (a misdecode or a trick)."""
        out = []
        offs = sorted(self.insn)
        for a, b in zip(offs, offs[1:]):
            if a + self.insn[a].size > b:
                out.append((a, b))
        return out

    def routines(self):
        """Per entry: the instructions it reaches without crossing another entry."""
        ents = sorted(self.entries)
        entset = set(ents)
        owners = {}
        self.rt = {}
        for e in ents:
            body, tails, falls, calls, ind = set(), set(), set(), set(), set()
            work = [e]
            while work:
                off = work.pop()
                if off in body or off not in self.insn:
                    continue
                body.add(off)
                owners.setdefault(off, []).append(e)
                ins = self.insn[off]
                succ, callee = self.flow(ins)
                if isinstance(callee, int):
                    calls.add(callee)
                elif callee:
                    ind.add(ins.address)
                    calls.update(self.call_tables.get(ins.address, []))
                if ins.address in self.indirect and not succ:
                    ind.add(ins.address)
                for s in succ:
                    if s is None:
                        continue
                    if s in entset and s != e:
                        if s == ins.address + ins.size and not (x86.X86_GRP_JUMP in ins.groups or ins.mnemonic == 'jmp'):
                            falls.add(s)
                        elif ins.mnemonic == 'jmp':
                            tails.add(s)
                        else:
                            tails.add(s)    # a conditional branch into another routine
                        continue
                    work.append(s)
            self.rt[e] = dict(body=body, tails=tails, falls=falls, calls=calls, ind=ind)
        self.owners = owners
        callers = {}
        for e, r in self.rt.items():
            for c in r['calls'] | r['tails'] | r['falls']:
                callers.setdefault(c, set()).add(e)
        self.callers = callers

    def extent(self, e):
        body = self.rt[e]['body']
        if not body:
            return e, e, 0
        lo = min(body)
        hi = max(o + self.insn[o].size for o in body)
        return lo, hi, sum(self.insn[o].size for o in body)

    def covered(self):
        cov = bytearray(CODE_END)
        for o, ins in self.insn.items():
            for i in range(ins.size):
                if o + i < CODE_END:
                    cov[o + i] = 1
        return cov

    def gaps(self, cov):
        out = []
        i = 0
        while i < CODE_END:
            if not cov[i]:
                j = i
                while j < CODE_END and not cov[j]:
                    j += 1
                out.append((i, j))
                i = j
            else:
                i += 1
        return out


def read_list(path, col=0):
    out = []
    if path and os.path.exists(path):
        for line in open(path):
            if line.startswith('#') or not line.strip():
                continue
            a = line.split()[col]
            if a.startswith('1000:'):
                out.append(int(a[5:], 16))
    return out


def read_names(path):
    names = {}
    if path and os.path.exists(path):
        for line in open(path):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if f[0].startswith('1000:'):
                names[int(f[0][5:], 16)] = f
    return names


def build(exe, coverage, ghidra):
    """The map: every seed walked, the routines split."""
    m = Map(load_image(exe))
    m.call_tables = {}
    for t in TABLES:
        tg = m.table_targets(t['loc'], t['count'])
        t['targets'] = tg
        for site in t['sites']:
            if t['kind'] == 'call':
                m.call_tables.setdefault(site, []).extend(tg)
            else:
                m.jump_targets.setdefault(site, []).extend(tg)
    for site, tg in CALL_SITE_TARGETS.items():
        m.call_tables.setdefault(site, []).extend(tg)
    for site, tg in SITE_TARGETS.items():
        m.jump_targets.setdefault(site, []).extend(tg)
    m.entries[0x0006] = {'start'}
    for o in read_list(ghidra):
        m.entries.setdefault(o, set()).add('ghidra')
    for o, why in EXTRA_ENTRIES.items():
        m.entries.setdefault(o, set()).add(why)
    for o in DEAD_ENTRIES:
        m.entries.setdefault(o, set()).add('dead')
    for t in TABLES:
        if t['kind'] == 'call':
            for o in t['targets']:
                m.entries.setdefault(o, set()).add('table')
    covseeds = read_list(coverage)
    m.walk(list(m.entries))
    m.walk(covseeds)
    m.routines()
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', nargs='?', default='re/micro.exe')
    ap.add_argument('--coverage', default='out/emu/cov/coverage.txt')
    ap.add_argument('--ghidra', default='re/micro.exe.funcs.txt')
    ap.add_argument('--names', default='tools/ghidra/names.tsv')
    ap.add_argument('--out')
    ap.add_argument('--list', action='store_true', help='one line per routine')
    ap.add_argument('--gaps', action='store_true', help='the unreached regions only')
    ap.add_argument('--tables', action='store_true', help='the tables and indirect sites only')
    ap.add_argument('--routine', help='one routine in detail')
    ap.add_argument('--tsv', action='store_true', help='entry, end, bytes, insns, callers, callees')
    ap.add_argument('--modules', action='store_true', help='per module of names.tsv: routines, ranges, bytes, insns, dependencies')
    ap.add_argument('--listing', help='every decoded instruction, by address, into this file (under re/)')
    a = ap.parse_args()
    if a.out and not os.path.abspath(a.out).startswith(os.path.abspath('re') + os.sep):
        sys.exit('--out writes only into re/')
    out = open(a.out, 'w') if a.out else sys.stdout
    m = build(a.exe, a.coverage, a.ghidra)
    names = read_names(a.names)
    cov = m.covered()
    gaps = m.gaps(cov)
    nm = lambda o: names[o][1] if o in names else 'sub_%04x' % o
    w = lambda s: out.write(s + '\n')

    if a.listing:
        if not os.path.abspath(a.listing).startswith(os.path.abspath('re') + os.sep):
            sys.exit('--listing writes only into re/')
        with open(a.listing, 'w') as f:
            for o in sorted(m.insn):
                ins = m.insn[o]
                if o in m.rt:
                    f.write('; ---- %s\n' % nm(o))
                f.write('1000:%04x  %-14s %s %s\n' % (o, ins.bytes.hex(), ins.mnemonic, ins.op_str))
        return
    if a.modules:
        mod = lambda o: names[o][2] if o in names else '?'
        stats = {}
        owner_of = {}
        for e in sorted(m.rt):
            for o in m.rt[e]['body']:
                owner_of.setdefault(o, e)      # shared code counts once, for the lowest entry
        for o, e in owner_of.items():
            st = stats.setdefault(mod(e), dict(b=0, i=0, ents=set(), lo=CODE_END, hi=0, deps={}))
            st['b'] += m.insn[o].size
            st['i'] += 1
        for e in sorted(m.rt):
            st = stats[mod(e)] if mod(e) in stats else stats.setdefault(mod(e), dict(b=0, i=0, ents=set(), lo=CODE_END, hi=0, deps={}))
            st['ents'].add(e)
            lo, hi, _ = m.extent(e)
            st['lo'], st['hi'] = min(st['lo'], lo), max(st['hi'], hi)
            r = m.rt[e]
            for c in r['calls'] | r['tails'] | r['falls']:
                if mod(c) != mod(e):
                    st['deps'][mod(c)] = st['deps'].get(mod(c), 0) + 1
        for k in sorted(stats, key=lambda k: stats[k]['lo']):
            st = stats[k]
            w('%-8s %3d routines %04x-%04x %6d bytes %5d insns  uses %s' % (
                k, len(st['ents']), st['lo'], st['hi'], st['b'], st['i'],
                ' '.join('%s(%d)' % d for d in sorted(st['deps'].items()))))
        return
    if a.tsv:
        for e in sorted(m.rt):
            lo, hi, n = m.extent(e)
            r = m.rt[e]
            w('1000:%04x\t%04x\t%d\t%d\t%s\t%s\t%s' % (
                e, hi, n, len(r['body']), nm(e),
                ','.join('%04x' % c for c in sorted(m.callers.get(e, ()))),
                ','.join('%04x' % c for c in sorted(r['calls'] | r['tails'] | r['falls']))))
        return
    if a.routine:
        e = int(a.routine.split(':')[-1], 16)
        r = m.rt[e]
        lo, hi, n = m.extent(e)
        w('%s 1000:%04x  extent %04x-%04x  %d bytes %d insns  (%s)' % (nm(e), e, lo, hi, n, len(r['body']), ','.join(sorted(m.entries[e]))))
        w('  callers: ' + ' '.join('%04x' % c for c in sorted(m.callers.get(e, ()))))
        w('  calls:   ' + ' '.join('%04x' % c for c in sorted(r['calls'])))
        w('  tails:   ' + ' '.join('%04x' % c for c in sorted(r['tails'])))
        w('  falls:   ' + ' '.join('%04x' % c for c in sorted(r['falls'])))
        w('  indirect: ' + ' '.join('%04x %s;' % (s, m.indirect.get(s, '')) for s in sorted(r['ind'])))
        return
    if a.tables or not (a.list or a.gaps):
        w('== tables')
        for t in TABLES:
            w('%s:%04x x%d %s used at %s -> %s  %s' % (t['loc'][0], t['loc'][1], t['count'], t['kind'],
              ','.join('%04x' % s for s in t['sites']), ' '.join('%04x' % x for x in t['targets']), t.get('what', '')))
        w('== indirect sites')
        for s in sorted(m.indirect):
            res = 'resolved' if s in m.jump_targets or s in m.call_tables else 'UNRESOLVED'
            own = m.owners.get(s, ['?'])
            w('1000:%04x %-28s in %s  %s' % (s, m.indirect[s], ','.join(nm(o) for o in own if o != '?') or '?', res))
        w('== far transfers (segments as in the file: 1424 is 2424 after the relocation)')
        for s in sorted(m.far):
            w('1000:%04x %s' % (s, m.far[s]))
        w('== self-modifying code (writer, patched address, what)')
        for wr, at, what in SELF_MODIFYING:
            w('1000:%04x -> 1000:%04x  %s' % (wr, at, what))
        w('== cs: memory operands (offsets at or above 93c0 are the data segment through cs)')
        for d in sorted(m.cs_refs):
            w('cs:%04x  from %s' % (d, ' '.join('%04x' % s for s in sorted(set(m.cs_refs[d])))))
    if a.list or not (a.tables or a.gaps):
        w('== routines (%d)' % len(m.rt))
        for e in sorted(m.rt):
            lo, hi, n = m.extent(e)
            r = m.rt[e]
            shared = sum(1 for o in r['body'] if len(m.owners[o]) > 1)
            w('1000:%04x %-28s %04x-%04x %5d b %4d i  callers %-3d calls %s%s%s%s%s' % (
                e, nm(e), lo, hi, n, len(r['body']), len(m.callers.get(e, ())),
                ' '.join('%04x' % c for c in sorted(r['calls'])),
                (' | tail ' + ' '.join('%04x' % c for c in sorted(r['tails']))) if r['tails'] else '',
                (' | falls ' + ' '.join('%04x' % c for c in sorted(r['falls']))) if r['falls'] else '',
                (' | ind ' + ' '.join('%04x' % c for c in sorted(r['ind']))) if r['ind'] else '',
                (' | shared %d' % shared) if shared else ''))
        orphans = sorted(o for o in m.insn if o not in m.owners)
        w('== code reached only from the harness coverage (no entry reaches it): %d insns' % len(orphans))
        run = []
        for o in orphans:
            if run and o != run[-1][1]:
                w('  %04x-%04x' % (run[0][0], run[-1][1]))
                run = []
            run.append((o, o + m.insn[o].size))
        if run:
            w('  %04x-%04x' % (run[0][0], run[-1][1]))
    if a.gaps or not (a.tables or a.list):
        regs = sorted(REGIONS)
        w('== unreached (%d regions, %d bytes of %d)' % (len(gaps), sum(j - i for i, j in gaps), CODE_END))
        for i, j in gaps:
            why = [r[2] for r in regs if r[0] < j and r[1] > i]
            blob = m.code[i:min(j, i + 16)]
            w('  %04x-%04x %5d  %s  %s' % (i, j, j - i, blob.hex(), '; '.join(why) if why else '?'))
        for o, b in m.overlaps():
            w('  overlap: insns at %04x and %04x' % (o, b))
        for o, e in m.errors:
            w('  error at %04x: %s' % (o, e))
    mapped = sum(cov)
    w('== summary: %d routines, %d bytes decoded as code, %d unreached, %d insns' % (len(m.rt), mapped, CODE_END - mapped, len(m.insn)))


if __name__ == '__main__':
    main()
