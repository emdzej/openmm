"""The reference harness: MICRO.EXE (or SM.EXE) from the user's copy run in Unicorn on a minimal DOS, BIOS
and PC, so the original can be watched, traced and compared with the port. Not part of the port: the
port is the C in src/, ported from the disassembly; this runs the original machine code as it is.

The machine, as far as the game needs it (docs/re/architecture.md):
- 1 MB of memory; the PSP at 0FF0h, the program at 1000h (the numbering of src/exe.h and Ghidra);
- DOS (int 21h): the file calls over the data folder (writes go to a scratch folder), vectors, the DTA,
  find first/next, date and time, exit;
- BIOS: int 10h (modes, the DAC block calls), 16h, 1Ah, 15h; no mouse (int 33h returns 0);
- the PIT (channel 0, mode 2/3, latched reads), the PIC (EOI, the mask), the VGA (the DAC, 3DAh with a
  70 Hz retrace, the CRTC and attribute registers recorded), the keyboard (port 60h with IRQ 1), the game
  port (201h: nothing connected), the AdLib (388h/389h: register writes recorded).

Time is virtual and deterministic: the CPU runs in slices of SLICE instructions, each worth
SLICE * TICKS_PER_INSN PIT ticks, and every port read costs a tick, so a polling loop sees the clock
move. Hardware interrupts are delivered between slices through the vectors the program installed."""
import os
import struct
import zlib

import unicorn
from unicorn import x86_const as X

PSP_SEG = 0x0FF0
LOAD_SEG = PSP_SEG + 0x10
ENV_SEG = 0x0F00
BIOS_SEG = 0xF000          # a stub IRET for every vector the program did not install
PIT_HZ = 1193182
ASCII = {0x1c: 0x0d, 0x01: 0x1b, 0x39: 0x20, 0x0e: 0x08, 0x0f: 0x09}
TICKS_PER_INSN = 0.1       # about a 12 MIPS machine
SLICE = 1000
FRAME_TICKS = PIT_HZ / 70.086
RETRACE_TICKS = 300        # how long 3DAh bit 3 reads set at the end of each frame

R = {n: getattr(X, 'UC_X86_REG_' + n) for n in
     'AX BX CX DX SI DI BP SP CS DS ES SS IP FLAGS'.split()}


EGA_DAC = bytes(c for i in range(64) for c in (
    ((i >> 2) & 1) * 42 + ((i >> 5) & 1) * 21, ((i >> 1) & 1) * 42 + ((i >> 4) & 1) * 21,
    (i & 1) * 42 + ((i >> 3) & 1) * 21))
EGA_ATTR = bytes([0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63])


class Vga:
    """The VGA's memory as four planes behind the sequencer and graphics controller (write modes 0-3,
    read modes 0-1, the latches), for the planar modes (FONT.BIN's 10h). Chain-4 (mode 13h) maps
    byte i to plane i & 3 at i >> 2, so a linear frame is read straight back."""

    def __init__(self):
        self.planes = [bytearray(65536) for _ in range(4)]
        self.seq_idx, self.seq = 0, bytearray(8)
        self.gc_idx, self.gc = 0, bytearray(16)
        self.latch = [0, 0, 0, 0]
        self.seq[2], self.seq[4], self.gc[8] = 0x0f, 0x06, 0xff

    def chain4(self):
        return bool(self.seq[4] & 0x08)

    def read(self, off):
        if self.chain4():
            return self.planes[off & 3][off >> 2]
        self.latch = [p[off] for p in self.planes]
        if self.gc[5] & 0x08:                   # read mode 1: colour compare
            cc, dc = self.gc[2], self.gc[7]
            v = 0xff
            for i in range(4):
                if dc >> i & 1:
                    v &= self.latch[i] if cc >> i & 1 else ~self.latch[i] & 0xff
            return v
        return self.latch[self.gc[4] & 3]

    def write(self, off, cpu):
        if self.chain4():
            if self.seq[2] >> (off & 3) & 1:
                self.planes[off & 3][off >> 2] = cpu
            return
        mode, rot, fn = self.gc[5] & 3, self.gc[3] & 7, self.gc[3] >> 3 & 3
        sr, esr, mask = self.gc[0], self.gc[1], self.gc[8]
        if mode == 1:
            for i in range(4):
                if self.seq[2] >> i & 1:
                    self.planes[i][off] = self.latch[i]
            return
        if mode in (0, 3):
            cpu = ((cpu >> rot) | (cpu << (8 - rot))) & 0xff
        if mode == 3:
            mask &= cpu
        for i in range(4):
            if not self.seq[2] >> i & 1:
                continue
            if mode == 0:
                v = (0xff if sr >> i & 1 else 0) if esr >> i & 1 else cpu
            elif mode == 2:
                v = 0xff if cpu >> i & 1 else 0
            else:
                v = 0xff if sr >> i & 1 else 0
            l = self.latch[i]
            v = v if fn == 0 else v & l if fn == 1 else v | l if fn == 2 else v ^ l
            self.planes[i][off] = (v & mask) | (l & ~mask & 0xff)


class Exit(Exception):
    pass


class Machine:
    def __init__(self, exe_path, data_dir, write_dir, cmdline=b'', log=None):
        self.data_dir, self.write_dir = data_dir, write_dir
        os.makedirs(write_dir, exist_ok=True)
        self.log = log or (lambda m: None)
        self.mu = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_16)
        self.mu.mem_map(0, 0xA0000)
        self.vga = Vga()
        self.vram_ram = False        # A000 as plain memory while chain-4 is on (mode 13h), else the planes
        self.mu.mmio_map(0xA0000, 0x10000, self._vram_read, None, self._vram_write, None)
        self.mu.mem_map(0xB0000, 0x60000)
        self.ticks = 0.0
        self.insns = 0
        self.exit_code = None
        # devices
        self.dac = bytearray(768)
        self.dac_w = self.dac_r = 0
        self.crtc_idx, self.crtc = 0, bytearray(256)
        self.attr_flip, self.attr_idx, self.attr = False, 0, bytearray(32)
        self.pit_div, self.pit_latch, self.pit_read_hi, self.pit_mode = 0x10000, None, False, 0x36
        self.pit_last = 0.0          # when channel 0 was last loaded or expired
        self.pic_mask = 0xb8
        self.in_service = False
        self.key_queue, self.port60 = [], 0
        self.irq_pending = set()
        self.opl_idx, self.opl_writes = 0, []
        self.video_mode = 3
        self.files, self.next_handle = {}, 5
        self.cwd = ''                # relative to data_dir, '/'-separated
        self.dta = (PSP_SEG, 0x80)
        self.find_state = None
        self.ports_seen = {}
        self.cmdline = cmdline
        self._exe_name = os.path.basename(exe_path).upper()
        self._setup_memory(open(exe_path, 'rb').read(), cmdline)
        self.bios_keys = []          # (scan, ascii) in the BIOS keyboard buffer
        self.code_card = False       # answer FONT.BIN's code-card check (reference runs only)
        self.irq_hold = False        # no interrupts delivered (tools/emu/capture.py, while recording one routine)
        self.on_io = None            # callback(kind 'I'/'O', port, value) for every port access
        self.on_int = None           # callback(n, phase 'pre'/'post') around every serviced int
        self.mu.hook_add(unicorn.UC_HOOK_INTR, self._intr)
        self.mu.hook_add(unicorn.UC_HOOK_CODE, self._bios_stub, None, BIOS_SEG * 16, BIOS_SEG * 16 + 255)
        self.mu.hook_add(unicorn.UC_HOOK_CODE, self._card, None, 0x24240 + 0xab, 0x24240 + 0x13a)
        self.skip_card = False       # return FONT.BIN's "pass" at once (reference runs of the game only)
        self.mu.hook_add(unicorn.UC_HOOK_CODE, self._overlay_call, None, 0x24240, 0x24240)
        self.mu.hook_add(unicorn.UC_HOOK_CODE, self._skip, None, 0x10000 + 0x3215, 0x10000 + 0x3215)
        self.mu.hook_add(unicorn.UC_HOOK_INSN, self._in, None, 1, 0, X.UC_X86_INS_IN)
        self.mu.hook_add(unicorn.UC_HOOK_INSN, self._out, None, 1, 0, X.UC_X86_INS_OUT)

    # ---------------------------------------------------------------- memory and registers

    def r(self, n):
        return self.mu.reg_read(R[n])

    def w(self, n, v):
        self.mu.reg_write(R[n], v & 0xffff)

    def rd(self, addr, n):
        return bytes(self.mu.mem_read(addr, n))

    def wr(self, addr, data):
        self.mu.mem_write(addr, bytes(data))

    def u16(self, addr):
        return struct.unpack('<H', self.rd(addr, 2))[0]

    def cstr(self, seg, off, terminator=b'\0'):
        out = b''
        while len(out) < 256:
            c = self.rd(seg * 16 + off + len(out), 1)
            if c == terminator:
                break
            out += c
        return out

    def set_cf(self, on):
        f = self.r('FLAGS')
        self.w('FLAGS', (f | 1) if on else (f & ~1))

    def _bios_stub(self, mu, addr, size, _):
        """The BIOS's own handlers where the program did not install one: the timer counts the day's ticks
        at 0040:006C, both acknowledge their interrupt."""
        n = addr - BIOS_SEG * 16
        if n == 8:
            t = (struct.unpack('<I', self.rd(0x46c, 4))[0] + 1) % 0x1800b0
            self.wr(0x46c, struct.pack('<I', t))
            self.in_service = False
        elif n == 9:
            self.in_service = False

    def bios_owns_keyboard(self):
        return self.irq_vector(9) == (9, BIOS_SEG)

    def _card(self, mu, addr, size, _):
        # FONT.BIN's two compares of the chosen symbol (AL) with the card's (DL): cmp al, dl
        if self.code_card and addr in (0x24240 + 0xab, 0x24240 + 0xe9, 0x24240 + 0x13a) and \
                self.rd(addr, 2) == b'\x3a\xc2':
            ax = self.r('AX')
            self.w('AX', (ax & 0xff00) | (self.r('DX') & 0xff))
            self.log('code card: answered %d at %04x' % (self.r('DX') & 0xff, addr - 0x24240))

    def _overlay_call(self, mu, addr, size, _):
        sp = self.r('SP')
        ip, cs = struct.unpack('<HH', self.rd(self.r('SS') * 16 + sp, 4))
        self.log('far call 2424:0000 ax=%04x from %04x:%04x (overlay starts %s)' % (
            self.r('AX'), cs, (ip - 5) & 0xffff, self.rd(0x24240, 4).hex()))

    def _skip(self, mu, addr, size, _):
        # Start's code-card call (1000:3215, lcall 2424:0000 with FONT.BIN loaded): "passed", at once
        if self.skip_card and self.rd(0x24240, 4) == b'PSQR':
            self.w('AX', 0)
            self.w('IP', 0x321a)
            self.log('code card skipped')

    def _vram_read(self, uc, off, size, _):
        v = 0
        for i in range(size):
            v |= self.vga.read((off + i) & 0xffff) << (8 * i)
        return v

    def _vram_write(self, uc, off, size, value, _):
        for i in range(size):
            self.vga.write((off + i) & 0xffff, (value >> (8 * i)) & 0xff)

    def _setup_memory(self, d, cmdline):
        last, blocks, nrel, hdr = struct.unpack_from('<HHHH', d, 2)
        ss, sp, _csum, ip, cs, reloff = struct.unpack_from('<HHHHHH', d, 14)
        size = blocks * 512 - (512 - last if last else 0)
        img = bytearray(d[hdr * 16:size])
        for i in range(nrel):
            off, seg = struct.unpack_from('<HH', d, reloff + 4 * i)
            p = seg * 16 + off
            struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + LOAD_SEG) & 0xffff)
        self.wr(LOAD_SEG * 16, img)
        # the interrupt vectors: every one at a BIOS stub IRET
        for n in range(256):
            self.wr(n * 4, struct.pack('<HH', n, BIOS_SEG))
        self.wr(BIOS_SEG * 16, b'\xcf' * 256)
        # BIOS data: the video mode, columns, the CRTC's port (0040:0063, which the game reads to find
        # 3DAh), the rows, the equipment word (a VGA, no game port)
        self.wr(0x449, b'\x03')
        self.wr(0x44a, struct.pack('<H', 80))
        self.wr(0x463, struct.pack('<H', 0x3d4))
        self.wr(0x484, b'\x18')
        self.wr(0x410, struct.pack('<H', 0x0021))
        env = b'COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0' + b'\x01\0' + b'C:\\MICROM\\' + \
            self._exe_name.encode() + b'\0'
        self.wr(ENV_SEG * 16, env)
        psp = bytearray(256)
        psp[0:2] = b'\xcd\x20'
        struct.pack_into('<H', psp, 2, 0xA000)
        struct.pack_into('<H', psp, 0x2c, ENV_SEG)
        psp[0x80] = len(cmdline)
        psp[0x81:0x81 + len(cmdline) + 1] = cmdline + b'\r'
        self.wr(PSP_SEG * 16, psp)
        for n, v in (('CS', cs + LOAD_SEG), ('IP', ip), ('SS', ss + LOAD_SEG), ('SP', sp), ('DS', PSP_SEG),
                     ('ES', PSP_SEG)):
            self.w(n, v)
        self.w('FLAGS', 0x0202)

    # ---------------------------------------------------------------- the run loop

    def irq_vector(self, n):
        return struct.unpack('<HH', self.rd(n * 4, 4))

    def deliver(self, vec):
        """A hardware interrupt: FLAGS, CS, IP pushed, IF and TF cleared, CS:IP from the vector."""
        off, seg = self.irq_vector(vec)
        sp = (self.r('SP') - 6) & 0xffff
        ss = self.r('SS')
        self.wr(ss * 16 + sp, struct.pack('<HHH', self.r('IP'), self.r('CS'), self.r('FLAGS')))
        self.w('SP', sp)
        self.w('FLAGS', self.r('FLAGS') & ~0x0300)
        self.w('CS', seg)
        self.w('IP', off)
        self.in_service = True

    def pit_period(self):
        return self.pit_div or 0x10000

    def sync_vram(self):
        """Between slices: A000 as plain RAM in chain-4 (fast; byte i is plane i & 3 at i >> 2), as the
        planar model otherwise; the contents move across on each switch."""
        want = self.vga.chain4()
        if want == self.vram_ram:
            return
        p = self.vga.planes
        if want:
            lin = bytearray(65536)
            for i in range(65536):
                lin[i] = p[i & 3][i >> 2]
            self.mu.mem_unmap(0xA0000, 0x10000)
            self.mu.mem_map(0xA0000, 0x10000)
            self.mu.mem_write(0xA0000, bytes(lin))
        else:
            lin = self.rd(0xA0000, 65536)
            for i in range(65536):
                p[i & 3][i >> 2] = lin[i]
            self.mu.mem_unmap(0xA0000, 0x10000)
            self.mu.mmio_map(0xA0000, 0x10000, self._vram_read, None, self._vram_write, None)
        self.vram_ram = want

    def planes(self):
        """The four planes, current (from RAM while chain-4 is mapped as RAM)."""
        if self.vram_ram:
            lin = self.rd(0xA0000, 65536)
            return [lin[k::4] + bytes(65536 - len(lin[k::4])) for k in range(4)]
        return self.vga.planes

    def step(self, slices=1):
        for _ in range(slices):
            self.sync_vram()
            if self.exit_code is not None:
                raise Exit(self.exit_code)
            # the timer: channel 0 expires every period ticks from its last load
            if self.ticks - self.pit_last >= self.pit_period():
                self.pit_last += self.pit_period() * int((self.ticks - self.pit_last) // self.pit_period())
                self.irq_pending.add(0)
            if self.key_queue and self.bios_owns_keyboard():
                code = self.key_queue.pop(0)
                if not code & 0x80:
                    self.bios_keys.append((code, ASCII.get(code, 0)))
            elif self.key_queue and 1 not in self.irq_pending:
                self.port60 = self.key_queue.pop(0)
                self.irq_pending.add(1)
            if self.r('FLAGS') & 0x200 and not self.in_service and not self.irq_hold:
                for irq in (0, 1):
                    if irq in self.irq_pending and not (self.pic_mask >> irq) & 1:
                        self.irq_pending.discard(irq)
                        self.deliver(8 + irq)
                        break
            start = self.r('CS') * 16 + self.r('IP')
            try:
                self.mu.emu_start(start, 0, count=SLICE)
            except unicorn.UcError as e:
                raise RuntimeError('%s at %04x:%04x' % (e, self.r('CS'), self.r('IP')))
            self.insns += SLICE
            self.ticks += SLICE * TICKS_PER_INSN

    # ---------------------------------------------------------------- ports

    def in_retrace(self):
        return (self.ticks % FRAME_TICKS) >= FRAME_TICKS - RETRACE_TICKS

    def _in(self, mu, port, size, _):
        self.ticks += 1
        self.ports_seen[('in', port)] = self.ports_seen.get(('in', port), 0) + 1
        v = 0xff
        if port == 0x3da:
            self.attr_flip = False
            v = 0x09 if self.in_retrace() else 0x00
        elif port == 0x3c2:
            v = 0x00                       # no vertical retrace interrupt pending (no IRQ 2 from this VGA)
        elif port == 0x3c9:
            v = self.dac[self.dac_r]
            self.dac_r = (self.dac_r + 1) % 768
        elif port == 0x3d5:
            v = self.crtc[self.crtc_idx]
        elif port == 0x3c5:
            v = self.vga.seq[self.vga.seq_idx]
        elif port == 0x3cf:
            v = self.vga.gc[self.vga.gc_idx]
        elif port == 0x40:
            if self.pit_latch is None:
                self.pit_latch = int(self.pit_period() - (self.ticks - self.pit_last) % self.pit_period()) & 0xffff
                self.pit_read_hi = False
            if not self.pit_read_hi:
                v, self.pit_read_hi = self.pit_latch & 0xff, True
            else:
                v, self.pit_latch = self.pit_latch >> 8, None
        elif port == 0x21:
            v = self.pic_mask
        elif port == 0x60:
            v = self.port60
        elif port == 0x61:
            v = 0x00
        elif port == 0x64:
            v = 0x1c                       # input buffer empty, system flag
        elif port == 0x201:
            v = 0xff                       # no joystick: buttons up, axes timed out
        elif port == 0x388:
            v = 0x00
        if self.on_io:
            self.on_io('I', port, v)
        return v

    def _out(self, mu, port, size, value, _):
        self.ports_seen[('out', port)] = self.ports_seen.get(('out', port), 0) + 1
        if size == 2:                      # out dx, ax: the index then the data at port + 1
            self._out(mu, port, 1, value & 0xff, None)
            self._out(mu, port + 1, 1, value >> 8, None)
            return
        if self.on_io:
            self.on_io('O', port, value)
        if port == 0x3c8:
            self.dac_w = value * 3
        elif port == 0x3c7:
            self.dac_r = value * 3
        elif port == 0x3c9:
            self.dac[self.dac_w] = value & 0x3f
            self.dac_w = (self.dac_w + 1) % 768
        elif port == 0x3c4:
            self.vga.seq_idx = value & 7
        elif port == 0x3c5:
            self.vga.seq[self.vga.seq_idx] = value
        elif port == 0x3ce:
            self.vga.gc_idx = value & 15
        elif port == 0x3cf:
            self.vga.gc[self.vga.gc_idx] = value
        elif port == 0x3d4:
            self.crtc_idx = value
        elif port == 0x3d5:
            self.crtc[self.crtc_idx] = value
        elif port == 0x3c0:
            if not self.attr_flip:
                self.attr_idx = value
            else:
                self.attr[self.attr_idx & 31] = value
            self.attr_flip = not self.attr_flip
        elif port == 0x43:
            if (value >> 4) & 3 == 0:      # latch channel 0
                if value >> 6 == 0:
                    self.pit_latch = int(self.pit_period() - (self.ticks - self.pit_last) % self.pit_period()) & 0xffff
                    self.pit_read_hi = False
            elif value >> 6 == 0:
                self.pit_mode = value
                self.pit_lo = None
        elif port == 0x40:
            if getattr(self, 'pit_lo', None) is None:
                self.pit_lo = value
            else:
                self.pit_div = (value << 8 | self.pit_lo) or 0x10000
                self.pit_lo = None
                self.pit_last = self.ticks
        elif port == 0x20:
            if value == 0x20 or 0x60 <= value <= 0x67:   # end of interrupt, non-specific or specific
                self.in_service = False
        elif port == 0x21:
            self.pic_mask = value
        elif port == 0x388:
            self.opl_idx = value
        elif port == 0x389:
            self.opl_writes.append((self.insns, self.opl_idx, value))

    # ---------------------------------------------------------------- interrupts

    def _intr(self, mu, intno, _):
        if self.on_int:
            self.on_int(intno, 'pre')
        try:
            self._serve(mu, intno)
        finally:
            if self.on_int:
                self.on_int(intno, 'post')

    def _serve(self, mu, intno):
        ah = self.r('AX') >> 8
        try:
            if intno == 0x21:
                self.int21(ah)
            elif intno == 0x10:
                self.int10(ah)
            elif intno == 0x16:
                self.int16(ah)
            elif intno == 0x1a:
                if ah == 0:
                    t = int(self.ticks / 65536)
                    self.w('CX', t >> 16), self.w('DX', t), self.w('AX', 0)
                elif ah == 2:                         # RTC time: 00:00:00
                    self.w('CX', 0), self.w('DX', 0), self.set_cf(False)
                elif ah == 4:                         # RTC date: 1994-09-14
                    self.w('CX', 0x1994), self.w('DX', 0x0914), self.set_cf(False)
            elif intno == 0x33:
                self.w('AX', 0)                       # no mouse driver
            elif intno == 0x15:
                self.set_cf(True)
                self.w('AX', (0x86 << 8) | (self.r('AX') & 0xff))
            elif intno == 0x20:
                self.exit_code = 0
                mu.emu_stop()
            else:
                self.log('int %02xh ah=%02x at %04x:%04x ignored' % (intno, ah, self.r('CS'), self.r('IP')))
        except Exit:
            mu.emu_stop()

    def int10(self, ah):
        al = self.r('AX') & 0xff
        if self.r('AX') == 0x1301:
            text = self.rd(self.r('ES') * 16 + self.r('BP'), self.r('CX'))
            self.log('BIOS text at row %d col %d: %s' % (self.r('DX') >> 8, self.r('DX') & 0xff,
                     ' '.join(text.decode('cp437').split())))
        if ah == 0x00:
            self.video_mode = al & 0x7f
            self.wr(0x449, bytes([self.video_mode]))
            v = self.vga
            if self.video_mode == 0x13:
                v.seq[2], v.seq[4], v.gc[5], v.gc[8] = 0x0f, 0x0e, 0x40, 0xff
                self.crtc[0x13] = 40
            else:
                v.seq[2], v.seq[4], v.gc[5], v.gc[8] = 0x0f, 0x06, 0x00, 0xff
                self.crtc[0x13] = 40
                self.dac[:192] = EGA_DAC
                self.attr[:16] = EGA_ATTR
            v.gc[0] = v.gc[1] = v.gc[3] = 0
            self.crtc[0x0c] = self.crtc[0x0d] = 0
            if not al & 0x80:
                for p in v.planes:
                    p[:] = bytes(65536)
        elif ah == 0x0f:
            self.w('AX', 40 << 8 | self.video_mode if self.video_mode == 0x13 else 80 << 8 | self.video_mode)
            self.w('BX', self.r('BX') & 0xff)
        elif ah == 0x10:
            if al == 0x12:                            # set a block of DAC registers from ES:DX
                first, n = self.r('BX'), self.r('CX')
                data = self.rd(self.r('ES') * 16 + self.r('DX'), 3 * n)
                self.dac[first * 3:first * 3 + 3 * n] = bytes(b & 0x3f for b in data)
            elif al == 0x10:                          # one DAC register
                i = self.r('BX') * 3
                self.dac[i:i + 3] = bytes([self.r('DX') >> 8 & 0x3f, self.r('CX') >> 8 & 0x3f, self.r('CX') & 0x3f])
            elif al == 0x17:                          # read a block into ES:DX
                first, n = self.r('BX'), self.r('CX')
                self.wr(self.r('ES') * 16 + self.r('DX'), self.dac[first * 3:first * 3 + 3 * n])
        # 06h (scroll), 09h (write char) and the rest only matter in text mode

    def int16(self, ah):
        if ah in (0x01, 0x11):
            f = self.r('FLAGS')
            if self.bios_keys:
                scan, asc = self.bios_keys[0]
                self.w('AX', scan << 8 | asc), self.w('FLAGS', f & ~0x40)
            else:
                self.w('FLAGS', f | 0x40)            # ZF: no key
        elif ah in (0x00, 0x10):
            if self.bios_keys:
                scan, asc = self.bios_keys.pop(0)
                self.w('AX', scan << 8 | asc)
            else:
                self.w('IP', self.r('IP') - 2)       # wait: the int 16h again

    # ---------------------------------------------------------------- DOS

    def host_path(self, name, for_write=False):
        """A DOS path relative to the current directory, resolved case-insensitively under data_dir; for
        writing, a file written before is read back from write_dir first."""
        name = name.replace('\\', '/')
        if len(name) > 1 and name[1] == ':':
            name = name[2:]
        parts = [] if name.startswith('/') else [p for p in self.cwd.split('/') if p]
        for p in name.split('/'):
            if p in ('', '.'):
                continue
            if p == '..':
                if parts:
                    parts.pop()
            else:
                parts.append(p.upper())
        rel = '/'.join(parts)
        wpath = os.path.join(self.write_dir, rel.replace('/', '__'))
        if for_write or os.path.exists(wpath):
            return wpath, rel
        cur = self.data_dir
        for p in parts:
            match = next((e for e in os.listdir(cur) if e.upper() == p), None) if os.path.isdir(cur) else None
            cur = os.path.join(cur, match if match else p)
        return cur, rel

    def int21(self, ah):
        al = self.r('AX') & 0xff
        ds, dx = self.r('DS'), self.r('DX')
        if ah == 0x30:
            self.w('AX', 0x0005), self.w('BX', 0), self.w('CX', 0)
        elif ah == 0x09:
            self.log('DOS print: %r' % self.cstr(ds, dx, b'$'))
        elif ah in (0x02, 0x06):
            pass
        elif ah == 0x19:
            self.w('AX', (self.r('AX') & 0xff00) | 2)  # C:
        elif ah == 0x0e:
            self.w('AX', (self.r('AX') & 0xff00) | 26)
        elif ah == 0x1a:
            self.dta = (ds, dx)
        elif ah == 0x2f:
            self.w('ES', self.dta[0]), self.w('BX', self.dta[1])
        elif ah == 0x25:
            self.wr(al * 4, struct.pack('<HH', dx, ds))
        elif ah == 0x35:
            off, seg = self.irq_vector(al)
            self.w('ES', seg), self.w('BX', off)
        elif ah == 0x2a:
            self.w('CX', 1994), self.w('DX', 0x090e), self.w('AX', (self.r('AX') & 0xff00) | 3)
        elif ah == 0x2c:
            t = int(self.ticks / PIT_HZ * 100)
            self.w('CX', (t // 360000 % 24) << 8 | (t // 6000 % 60)), self.w('DX', (t // 100 % 60) << 8 | t % 100)
        elif ah in (0x2b, 0x2d):
            self.w('AX', self.r('AX') & 0xff00)
        elif ah == 0x3b:
            path = self.cstr(ds, dx).decode('latin-1')
            host, rel = self.host_path(path)
            ok = os.path.isdir(host)
            if ok:
                self.cwd = rel
            self.set_cf(not ok)
            if not ok:
                self.w('AX', 3)
            self.log('chdir %r -> %s' % (path, 'ok' if ok else 'missing'))
        elif ah == 0x47:
            self.wr(ds * 16 + self.r('SI'), self.cwd.replace('/', '\\').encode() + b'\0')
            self.set_cf(False)
        elif ah in (0x3d, 0x3c):
            path = self.cstr(ds, dx).decode('latin-1')
            write = ah == 0x3c or (al & 3) != 0
            host, rel = self.host_path(path, for_write=ah == 0x3c)
            try:
                if ah == 0x3c:
                    f = open(host, 'w+b')
                elif write:
                    data = open(host, 'rb').read()
                    wpath = os.path.join(self.write_dir, rel.replace('/', '__'))
                    open(wpath, 'wb').write(data)
                    f = open(wpath, 'r+b')
                else:
                    f = open(host, 'rb')
            except OSError:
                self.log('open %r: not found' % path)
                self.w('AX', 2), self.set_cf(True)
                return
            h = self.next_handle
            self.next_handle += 1
            self.files[h] = (f, rel)
            self.w('AX', h), self.set_cf(False)
            self.log('open %r (%s) -> %d' % (path, 'create' if ah == 0x3c else 'rw' if write else 'r', h))
        elif ah == 0x3e:
            f = self.files.pop(self.r('BX'), None)
            if f:
                f[0].close()
            self.set_cf(f is None)
        elif ah == 0x3f:
            f = self.files.get(self.r('BX'))
            if not f:
                self.w('AX', 6), self.set_cf(True)
                return
            data = f[0].read(self.r('CX'))
            self.wr(ds * 16 + dx, data)
            self.w('AX', len(data)), self.set_cf(False)
        elif ah == 0x40:
            f = self.files.get(self.r('BX'))
            if self.r('BX') in (1, 2):
                self.w('AX', self.r('CX')), self.set_cf(False)
                return
            if not f:
                self.w('AX', 6), self.set_cf(True)
                return
            f[0].write(self.rd(ds * 16 + dx, self.r('CX')))
            self.w('AX', self.r('CX')), self.set_cf(False)
        elif ah == 0x42:
            f = self.files.get(self.r('BX'))
            if not f:
                self.w('AX', 6), self.set_cf(True)
                return
            off = (self.r('CX') << 16) | dx
            if off & 0x80000000:
                off -= 1 << 32
            pos = f[0].seek(off, al)
            self.w('DX', pos >> 16), self.w('AX', pos), self.set_cf(False)
        elif ah == 0x4a:
            self.set_cf(False)
        elif ah == 0x48:
            self.w('AX', 8), self.w('BX', 0), self.set_cf(True)
        elif ah == 0x4c:
            self.exit_code = al
            self.mu.emu_stop()
        elif ah in (0x4e, 0x4f):
            if ah == 0x4e:
                pat = self.cstr(ds, dx).decode('latin-1')
                host, rel = self.host_path(pat)
                d, mask = os.path.dirname(host), os.path.basename(rel)
                import fnmatch
                names = sorted(e for e in (os.listdir(d) if os.path.isdir(d) else [])
                               if fnmatch.fnmatch(e.upper(), mask.upper().replace('*.*', '*')))
                self.find_state = [(e, os.path.join(d, e)) for e in names]
                self.log('find %r -> %d' % (pat, len(names)))
            if not self.find_state:
                self.w('AX', 18), self.set_cf(True)
                return
            name, path = self.find_state.pop(0)
            seg, off = self.dta
            rec = bytearray(43)
            rec[21] = 0x10 if os.path.isdir(path) else 0x20
            struct.pack_into('<I', rec, 26, 0 if os.path.isdir(path) else os.path.getsize(path))
            rec[30:30 + len(name)] = name.upper().encode()
            self.wr(seg * 16 + off, rec)
            self.set_cf(False)
        else:
            self.log('int 21h ah=%02x al=%02x at %04x:%04x not served' % (ah, al, self.r('CS'), self.r('IP')))
            self.set_cf(True)

    # ---------------------------------------------------------------- the screen

    def frame(self):
        """The screen as (w, h, rgb): mode 13h from the chained planes, the 16-colour planar modes from
        the CRTC's start address through the attribute palette; colours through the DAC (6-bit values
        widened as the DAC does)."""
        pal = bytes((c << 2) | (c >> 4) for c in self.dac)
        planes = self.planes()
        if self.video_mode == 0x13:
            w, h = 320, 200
            idx = bytearray(w * h)
            for i in range(w * h):
                idx[i] = planes[i & 3][i >> 2]
        else:
            w, h = (640, 350) if self.video_mode == 0x10 else (640, 480) if self.video_mode == 0x12 else (320, 200)
            start = self.crtc[0x0c] << 8 | self.crtc[0x0d]
            pitch = self.crtc[0x13] * 2 or w // 8
            idx = bytearray(w * h)
            amap = [self.attr[c] & 0x3f for c in range(16)]
            for y in range(h):
                row = (start + y * pitch) & 0xffff
                for xb in range(w // 8):
                    a = (row + xb) & 0xffff
                    b0, b1, b2, b3 = planes[0][a], planes[1][a], planes[2][a], planes[3][a]
                    base = y * w + xb * 8
                    for bit in range(8):
                        s_ = 7 - bit
                        c = (b0 >> s_ & 1) | (b1 >> s_ & 1) << 1 | (b2 >> s_ & 1) << 2 | (b3 >> s_ & 1) << 3
                        idx[base + bit] = amap[c]
        out = bytearray(w * h * 3)
        for i, p in enumerate(idx):
            out[i * 3:i * 3 + 3] = pal[p * 3:p * 3 + 3]
        return w, h, bytes(out)


def write_png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)
