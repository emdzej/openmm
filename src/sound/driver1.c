/* DRIVER1.BIN, the AdLib driver (its own banner names a third-party sound-driver package of 1992-93),
   ported routine by routine (docs/re/sound.md). The file is a flat program the game loads at 2424:0000
   and calls far there with a function number in AH; its code is 0000-09d2, then its tables, its
   variables, and the music, effects and instruments (13f0/13f2/13f4 point at them). Everything is read
   and kept in that segment of g_mem.

   The sequencer: 16 voices of 16h bytes at 1271, each reading a byte stream (notes, delays, commands)
   and owning one of the OPL2's nine melodic channels ([+8]). Music voices (state 1) are started together
   from a tune's header; sound effects (state 2) one per queued request, on a free voice and a free
   channel. Every call of AH = 3 (the game's timer, once per frame) advances two 16.16 clocks, one for the
   music and one for the effects, and steps every voice by the whole ticks gained.

   Voice (DI = 1271 + 16h * n): +0 the stream pointer, +2/+4 the delay left (32-bit, signed: an event is
   due when it goes negative), +6 the loop mark, +8 the OPL channel, +9 the note, +0a the state (0 free,
   1 music, 2 effect), +0b the last B0h value (bit 5: key on), +0c/+0d the two operators' register
   offsets, +0e the instrument, +10 its number, +11 the volume, +12 the effect number, +13 the drum map
   flag, +14 the pitch bend range, +15 the pitch bend (40h = none).

   Variables: 00c3 the highest function number accepted (0 until initialised), 114d..124c a copy of the
   OPL2's 256 registers (writes of an unchanged value are skipped), 124d a stream of stops (command 9Ah
   jumps there), 1252 the AdLib's port, 1254 the rate (AH = 2's CX), 1256 the master volume, 1258 the
   number of music tracks, 125a the tune asked for, 125b the tune playing, 125c/125d stop the music / all
   at the next tick, 125e the tune's tempo and 125f its multiplier, 1260 busy (a tick in progress),
   1261..1268 the music clock (step, then value), 1269..1270 the effects' clock, 13d1 the effects queue
   (8), 13d9 the queue of effects to stop (8), 13f0/13f2/13f4 the music, effects and instruments. */
#include "sound.h"
#include "machine.h"
#include "pending.h"
#include <stdio.h>

void Driver1_IoDelay(void);
void Driver1_OplWrite(void);
void Driver1_OplWriteCached(void);
void Driver1_NoteOff(void);
void Driver1_LoadInstrument(void);
void Driver1_NoteOn(void);
void Driver1_NoteFreq(void);
void Driver1_PitchOffset(void);
void Driver1_SetVolume(void);
void Driver1_ReadDelta(void);
void Driver1_CalcRate(void);
void Driver1_StopEffectVoiceAt(void);
void Driver1_DoStopAll(void);

/* Driver1_IoDelay DRIVER1:01c1
   The AdLib's wait after a register write (BX = the count to wait down to: 1FFCh after the index, 1FEDh
   after the data, 3 and 18 PIT counts): channel 2 is started on 1FFFh in mode 3 with its gate on (and
   the speaker's data bit, port 61h bits 0 and 1), then latched and read until it is at or below BX,
   reloading it with what was read on every round; then the gate off. Reads of port 42h are those of the
   machine (the harness's and the port's: FFh, so one round). AX kept; CF = 0 from the last `and`. */
void Driver1_IoDelay(void)
{
    uint16_t ax = R.ax;
    io_out8(0x43, 0xb6);
    uint8_t al = io_in8(0x61);
    al |= 3;
    io_out8(0x61, al);
    uint16_t count = 0x1fff;
    for (;;) {
        io_out8(0x42, (uint8_t)count);
        io_out8(0x42, (uint8_t)(count >> 8));
        io_out8(0x43, 0x86);   /* latch channel 2 */
        io_out8(0x43, 0xb6);
        uint8_t lo = io_in8(0x42);
        uint8_t hi = io_in8(0x42);
        count = (uint16_t)(hi << 8 | lo);
        if (!((int16_t)count > (int16_t)R.bx)) break;   /* jg: signed */
    }
    al = (uint8_t)(io_in8(0x61) & 0xfe);
    io_out8(0x61, al);
    R.cf = false;
    flags_zs8(al);
    R.ax = ax;
}

/* Driver1_OplWrite DRIVER1:019d
   OPL2 register AL := AH, through the port at [1252] (index, then data at +1), the copy at 114d+AL
   updated. AX = AH:AH back, BX and DX kept. */
void Driver1_OplWrite(void)
{
    uint16_t bx = R.bx, dx = R.dx;
    o8w((uint16_t)(0x114d + AL), AH);
    R.dx = o16(0x1252);
    io_out8(R.dx, AL);
    R.bx = 0x1ffc;
    Driver1_IoDelay();
    SET_AL(AH);
    R.dx = (uint16_t)(R.dx + 1);
    io_out8(R.dx, AL);
    R.bx = 0x1fed;
    Driver1_IoDelay();
    R.dx = dx, R.bx = bx;
}

/* Driver1_OplWriteCached DRIVER1:0175
   As Driver1_OplWrite, but nothing when the copy already holds the value (ZF = 1, AX kept). */
void Driver1_OplWriteCached(void)
{
    uint16_t at = (uint16_t)(0x114d + AL);
    if (AH == o8(at)) {
        R.zf = true, R.sf = false, R.cf = false;
        return;
    }
    Driver1_OplWrite();
}

/* Driver1_Shutdown DRIVER1:0203
   AH = 1: the speaker's gate and data off (port 61h bits 0, 1) and the function limit [00c3] back to 0
   (every call but AH = 0 answers FFFFh from then on). AL = 0. */
void Driver1_Shutdown(void)
{
    uint8_t al = (uint8_t)(io_in8(0x61) & 0xfc);
    io_out8(0x61, al);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
    o8w(0x00c3, 0);
}

/* Driver1_Init DRIVER1:00e8 (and 00dc, its first branch)
   AH = 0. DX = the AdLib's port: with one given (the game passes 388h) only [1252] := DX and the
   functions allowed ([00c3] := 10h), AL = 0; the voices are not reset (they start zeroed in the file).
   With DX = 0, the classic detection at 388h: both timers reset, timer 1 started on FFh, a wait of FFFFh
   loops, and the status must go from no flags to 0C0h (timer 1 expired, IRQ); then waveform select on,
   rhythm off, CSM/note-select 0, the voices given channels 0..15 ([+8]), and the stop-all sequence run.
   AL = 1: no AdLib. */
void Driver1_Init(void)
{
    if (R.dx != 0) {
        o16w(0x1252, R.dx);
        o8w(0x00c3, 0x10);
        SET_AL(0);
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    R.dx = 0x388;
    o16w(0x1252, R.dx);
    R.ax = 0x0001; Driver1_OplWrite();
    R.ax = 0x6004; Driver1_OplWrite();
    R.ax = 0x8004; Driver1_OplWrite();
    SET_AL(io_in8(R.dx));
    uint16_t first = R.ax;
    R.ax = 0xff02; Driver1_OplWrite();
    R.ax = 0x2104; Driver1_OplWrite();
    R.cx = 0;   /* loop: FFFFh rounds of nothing (the timer's 80 us, at any CPU speed of the day) */
    SET_AL(io_in8(R.dx));
    uint16_t second = R.ax;
    R.ax = 0x6004; Driver1_OplWrite();
    R.ax = 0x8004; Driver1_OplWrite();
    R.ax = second;
    R.bx = first;
    SET_AL(AL & 0xe0);
    SET_BL(BL & 0xe0);
    if (BL != 0 || AL != 0xc0) {
        flags_zs8(BL != 0 ? BL : (uint8_t)(AL - 0xc0));
        R.cf = BL == 0 && AL < 0xc0;
        SET_AL(1);
        return;
    }
    R.ax = 0x2001; Driver1_OplWrite();
    R.ax = 0x00bd; Driver1_OplWrite();
    R.ax = 0x0008; Driver1_OplWrite();
    R.di = 0x1271;
    R.cx = 0x10;
    SET_AL(0);
    do {
        o8w((uint16_t)(R.di + 0x0a), CH);
        o8w((uint16_t)(R.di + 0x12), CH);
        o8w((uint16_t)(R.di + 8), AL);
        SET_AL(AL + 1);
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
    o8w(0x125d, 1);
    Driver1_DoStopAll();
    o8w(0x00c3, 0x10);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver1_CalcRate DRIVER1:098f
   The clocks' steps from the rate [1254] (AH = 2's CX): n = 444_6390h / rate; the music's step (16.16,
   [1263]:[1261]) = tempo [125e] * multiplier [125f] / n, the effects' ([126b]:[1269]) = 1680h / n. The
   fraction's division takes DX:AX = remainder:quotient of the first (the quotient left in AX: the
   original's), so the music's fraction is a little high when the step is one or more. A rate below 445h
   (the quotient would overflow): the music's step 0.0001h... ([1263] = 0, [1261] = 1), AL = 1. */
void Driver1_CalcRate(void)
{
    R.dx = 0x0444;
    R.ax = 0x6390;
    R.cx = o16(0x1254);
    if (R.cx < 0x445) {
        R.ax = 0;
        o16w(0x1263, R.ax);
        R.ax = 1;
        o16w(0x1261, R.ax);
        R.cf = true, R.zf = false, R.sf = false;   /* inc al leaves the cmp's carry */
        return;
    }
    uint32_t n = (uint32_t)R.dx << 16 | R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    R.cx = R.ax;
    R.ax = (uint16_t)(o8(0x125e) * o8(0x125f));
    R.dx = 0;
    n = R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x1263, R.ax);
    n = (uint32_t)R.dx << 16 | R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x1261, R.ax);
    R.ax = 0x1680;
    R.dx = 0;
    n = R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x126b, R.ax);
    n = (uint32_t)R.dx << 16 | R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x1269, R.ax);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver1_SetRate DRIVER1:020f
   AH = 2: the rate [1254] := CX (the game passes 4287h), the clocks' steps recomputed with interrupts
   off; the flags come back as they were. AL = 0, or 1 for a rate too low (the game repeats the call
   until AL = 0). */
void Driver1_SetRate(void)
{
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    o16w(0x1254, R.cx);
    Driver1_CalcRate();
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Driver1_ReadDelta DRIVER1:0925
   A delay from the stream at SI into DX:BX: bytes with bit 7 set carry 7 bits each, most significant
   first, and a byte with bit 7 clear ends it (value = (value + b & 7Fh) * 80h per long byte, then + the
   last); a fifth long byte gives FFFF_FFFFh. */
void Driver1_ReadDelta(void)
{
    R.dx = 0, R.bx = 0;
    SET_AL(lodsb());
    if (!(AL & 0x80)) {
        SET_BL(AL);
        R.cf = false;
        flags_zs8(AL);
        return;
    }
    R.ax &= 0x7f;
    R.ax = (uint16_t)(R.ax << 8);
    R.ax >>= 1;
    R.cx = 0x80;
    R.bx = R.ax;
    for (int i = 0; i < 4; i++) {
        SET_AL(lodsb());
        if (!(AL & 0x80)) {   /* the last byte (0987) */
            SET_AH(0);
            uint32_t s = (uint32_t)R.bx + R.ax;
            R.bx = (uint16_t)s;
            uint32_t d = (uint32_t)R.dx + (s >> 16);
            R.dx = (uint16_t)d;
            R.cf = d > 0xffff;
            flags_zs16(R.dx);
            return;
        }
        if (i == 3) break;
        R.ax &= 0x7f;
        uint32_t s = (uint32_t)R.ax + R.bx;
        R.ax = (uint16_t)s;
        R.dx = (uint16_t)(R.dx + (s >> 16));
        R.bx = R.dx;
        uint32_t p = (uint32_t)R.ax * R.cx;
        R.ax = (uint16_t)p, R.dx = (uint16_t)(p >> 16);
        R.dx = (uint16_t)(R.dx + R.bx);
        R.bx = R.ax;
    }
    R.dx = 0xffff;
    R.bx = R.dx;
    R.cf = false, R.zf = false, R.sf = true;   /* the or al, al before the jns */
}

/* Driver1_PitchOffset DRIVER1:0551
   The voice's pitch bend in 1/64 semitones: AX = (bend [+15] - 40h) * range [+14], signed. */
void Driver1_PitchOffset(void)
{
    uint8_t al = o8((uint16_t)(R.di + 0x15));
    bool below = al < 0x40;
    al = (uint8_t)(al - 0x40);
    if (!below) {
        R.ax = (uint16_t)(al * o8((uint16_t)(R.di + 0x14)));
        R.cf = AH != 0;
        return;
    }
    al = (uint8_t)-al;
    R.ax = (uint16_t)(al * o8((uint16_t)(R.di + 0x14)));
    R.cf = R.ax != 0;
    R.ax = (uint16_t)-R.ax;
    flags_zs16(R.ax);
}

/* Driver1_NoteFreq DRIVER1:0503
   The A0h/B0h values of note AL (in semitones, with the voice's pitch bend): x = note * 64 + bend; the
   frequency word from the table at 0A38h (300h entries: 12 semitones of 64 steps) at x mod 300h, the
   block from the table at 09D3h by the semitone x / 64; a table word with bit 15 set means the next
   block (BL + 4) and its low 13 bits; below semitone 7 the f-number is halved. AX = AH:AL = the B0h and
   A0h values (key off), BL = the block bits (BH = 09h), SI and DX kept. */
void Driver1_NoteFreq(void)
{
    uint16_t dx = R.dx, si = R.si;
    SET_AH(AL);
    SET_AL(0);
    R.ax >>= 2;
    R.bx = R.ax;
    Driver1_PitchOffset();
    R.ax = (uint16_t)(R.ax + R.bx);
    uint16_t x = R.ax;
    R.bx = 0x300;
    R.ax = (uint16_t)(R.ax + R.bx);
    do R.ax = (uint16_t)(R.ax - R.bx);
    while (R.ax >= R.bx);
    R.si = (uint16_t)(0x0a38 + (uint16_t)(R.ax << 1));
    R.ax = (uint16_t)(x << 2);
    SET_AL(AH);
    SET_DL(AL);
    R.bx = 0x09d3;
    SET_AL(o8((uint16_t)(R.bx + AL)));
    SET_BL(AL);
    R.ax = lodsw();
    if (AH & 0x80) {
        SET_AH(AH & 0x1f);
        SET_BL(BL + 4);
    }
    if (DL < 7) R.ax >>= 1;
    R.si = si, R.dx = dx;
    R.ax &= 0x1fff;
    SET_AH(AH | BL);
    R.cf = false;
    flags_zs8(AH);
}

/* Driver1_LoadInstrument DRIVER1:03c4
   The instrument at BX into the voice's channel: per operator (DL = [+0c] the modulator's register
   offset, DH = [+0d] the carrier's) 60h attack/decay from +0/+1, 80h sustain/release +2/+3, E0h
   waveform +6/+7, then C0h feedback/connection +9 on the channel, 20h tremolo/vibrato/sustain/KSR/
   multiple +4/+5. (40h, the levels, by Driver1_SetVolume.) DX kept. */
void Driver1_LoadInstrument(void)
{
    uint16_t dx = R.dx;
    R.dx = o16((uint16_t)(R.di + 0x0c));
    static const struct { uint8_t src, reg; int8_t op; } w[] = {
        {0, 0x60, 0}, {1, 0x60, 1}, {2, 0x80, 0}, {3, 0x80, 1}, {6, 0xe0, 0}, {7, 0xe0, 1},
        {9, 0xc0, -1}, {4, 0x20, 0}, {5, 0x20, 1},
    };
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) {
        SET_AH(o8((uint16_t)(R.bx + w[i].src)));
        uint8_t base = w[i].op < 0 ? o8((uint16_t)(R.di + 8)) : w[i].op ? DH : DL;
        SET_AL(base + w[i].reg);
        Driver1_OplWriteCached();
    }
    R.dx = dx;
}

/* Driver1_SetVolume DRIVER1:0567
   The two operators' 40h (KSL and total level) for the instrument BX at velocity CL: level =
   (base + 1) * (velocity + 1) / 2 * 8 / 2 * (volume [+11] + 1) * 2 / 256 * (master [1256] + 1) / 65536,
   turned into an attenuation by the table at 10CCh; the carrier's base is [+0b], with KSL [+0c] bits
   0-1 (into bits 6-7); the modulator's [+0a] goes through the same sum only with the additive
   connection ([+9] bit 0), else straight through the table, with KSL bits 4-5. DX kept; CX the
   velocity again after the carrier (and the last product's multiplier after an additive modulator). */
static uint8_t d1_level(uint8_t base)
{
    SET_CH(0);
    uint8_t al = (uint8_t)(base + 1);
    SET_CL(CL + 1);
    R.ax = (uint16_t)(al * CL);
    R.ax >>= 1;
    SET_CL(8);
    uint32_t p = (uint32_t)R.ax * R.cx;
    R.ax = (uint16_t)p, R.dx = (uint16_t)(p >> 16);
    bool c = DL & 1;
    SET_DL(DL >> 1);
    R.ax = (uint16_t)((R.ax >> 1) | (c ? 0x8000 : 0));
    SET_CL(o8((uint16_t)(R.di + 0x11)));
    SET_CL(CL + 1);
    R.cx = (uint16_t)(R.cx << 1);
    p = (uint32_t)R.ax * R.cx;
    R.ax = (uint16_t)p, R.dx = (uint16_t)(p >> 16);
    SET_AL(AH);
    SET_AH(DL);
    R.cx = (uint16_t)(o16(0x1256) + 1);
    p = (uint32_t)R.ax * R.cx;
    R.ax = (uint16_t)p, R.dx = (uint16_t)(p >> 16);
    return DL;
}

void Driver1_SetVolume(void)
{
    uint16_t dx = R.dx, cx = R.cx;
    SET_AL(d1_level(o8((uint16_t)(R.bx + 0x0b))));
    R.cx = cx;
    SET_AL(o8((uint16_t)(0x10cc + AL)));
    uint8_t ah = o8((uint16_t)(R.bx + 0x0c));
    ah = (uint8_t)((ah >> 2 | ah << 6) & 0xc0);
    SET_AH(ah | AL);
    SET_AL(o8((uint16_t)(R.di + 0x0d)) | 0x40);
    Driver1_OplWriteCached();
    SET_AL(o8((uint16_t)(R.bx + 0x0a)));
    if (o8((uint16_t)(R.bx + 9)) & 1) SET_AL(d1_level(AL));
    SET_AL(o8((uint16_t)(0x10cc + AL)));
    ah = (uint8_t)((o8((uint16_t)(R.bx + 0x0c)) << 2) & 0xc0);
    SET_AH(ah | AL);
    SET_AL(o8((uint16_t)(R.di + 0x0c)) | 0x40);
    Driver1_OplWriteCached();
    R.dx = dx;
}

/* Driver1_NoteOn DRIVER1:049e
   Note AL on the voice DI with the instrument BX. With the drum map ([+13] set) the note picks the
   instrument (the byte at [13f4] + note) and the instrument its fixed note (+8), the instrument loaded
   when it changes; otherwise the note is transposed by the instrument's +8 less 18h. Then A0h and B0h
   with key on (the B0h value kept in [+0b]). BX = the instrument, SI kept. */
void Driver1_NoteOn(void)
{
    uint16_t si = R.si;
    if (o8((uint16_t)(R.di + 0x13)) != 0) {
        R.bx = o16(0x13f4);
        SET_AL(o8((uint16_t)(R.bx + AL)));
        R.bx = o16((uint16_t)(R.di + 0x0e));
        if (AL != o8((uint16_t)(R.di + 0x10))) {
            o8w((uint16_t)(R.di + 0x10), AL);
            SET_AH(0);
            R.ax = (uint16_t)(R.ax << 4);
            R.bx = (uint16_t)(o16(0x13f4) + 0x80 + R.ax);
            o16w((uint16_t)(R.di + 0x0e), R.bx);
            Driver1_LoadInstrument();
        }
        SET_AL(o8((uint16_t)(R.bx + 8)));
        o8w((uint16_t)(R.di + 9), AL);
    } else {
        SET_AL(AL - 0x18);
        SET_AL(AL + o8((uint16_t)(R.bx + 8)));
    }
    uint16_t bx = R.bx;
    Driver1_NoteFreq();
    uint16_t ax = R.ax;
    SET_AH(AL);
    SET_AL(o8((uint16_t)(R.di + 8)) | 0xa0);
    Driver1_OplWriteCached();
    R.ax = ax;
    SET_AL(o8((uint16_t)(R.di + 8)) | 0xb0);
    SET_AH(AH | 0x20);
    o8w((uint16_t)(R.di + 0x0b), AH);
    Driver1_OplWriteCached();
    R.bx = bx;
    R.si = si;
}

/* Driver1_NoteOff DRIVER1:034a
   Key off on the voice's channel if its note is on ([+0b] bit 5): B0h without bit 5. Command 99h. */
void Driver1_NoteOff(void)
{
    SET_AH(o8((uint16_t)(R.di + 0x0b)));
    if (!(AH & 0x20)) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    SET_AH(AH & 0xdf);
    o8w((uint16_t)(R.di + 0x0b), AH);
    SET_AL(o8((uint16_t)(R.di + 8)) | 0xb0);
    Driver1_OplWriteCached();
}

/* Driver1_CmdNoteOffIf DRIVER1:0343
   Command 90h n: key off if the voice's note is n. */
void Driver1_CmdNoteOffIf(void)
{
    SET_AL(lodsb());
    if (AL != o8((uint16_t)(R.di + 9))) {
        R.zf = false;
        return;
    }
    Driver1_NoteOff();
}

/* Driver1_CmdEnd DRIVER1:0364
   Command 91h (and any byte above 9Dh): the voice ends, key off. When the last music voice ends the
   tune playing [125b] is none. */
void Driver1_CmdEnd(void)
{
    SET_AL(o8((uint16_t)(R.di + 0x0a)));
    o8w((uint16_t)(R.di + 0x0a), 0);
    o8w((uint16_t)(R.di + 0x12), 0);
    if (AL == 1) {
        uint16_t cx = R.cx, di = R.di;
        R.di = 0x1271;
        SET_AL(1);
        R.cx = 0x10;
        bool found = false;
        do {
            if (AL == o8((uint16_t)(R.di + 0x0a))) { found = true; break; }
            R.di = (uint16_t)(R.di + 0x16);
        } while (--R.cx);
        if (!found) o8w(0x125b, 0);
        R.di = di, R.cx = cx;
    }
    Driver1_NoteOff();
}

/* Driver1_CmdInstrument DRIVER1:0398
   Command 92h n: instrument n (the 16-byte records at [13f4] + 80h), volume 7Fh, no pitch bend, the
   bend range from the instrument's +0f, and the instrument loaded (falls into Driver1_LoadInstrument). */
void Driver1_CmdInstrument(void)
{
    o8w((uint16_t)(R.di + 0x11), 0x7f);
    o8w((uint16_t)(R.di + 0x15), 0x40);
    SET_AL(lodsb());
    o8w((uint16_t)(R.di + 0x10), AL);
    SET_AH(0);
    R.ax = (uint16_t)(R.ax << 4);
    R.bx = (uint16_t)(o16(0x13f4) + 0x80 + R.ax);
    o16w((uint16_t)(R.di + 0x0e), R.bx);
    SET_AL(o8((uint16_t)(R.bx + 0x0f)));
    o8w((uint16_t)(R.di + 0x14), AL);
    Driver1_LoadInstrument();
}

/* Driver1_CmdTempo DRIVER1:0430
   Command 93h n: the tempo multiplier [125f] := n, the clocks' steps recomputed (for the effects' too). */
void Driver1_CmdTempo(void)
{
    SET_AL(lodsb());
    o8w(0x125f, AL);
    Driver1_CalcRate();
}

/* Driver1_CmdPitchBend DRIVER1:0439
   Command 95h n: pitch bend n (40h none); a sounding note is retuned at once (A0h, B0h key on). */
void Driver1_CmdPitchBend(void)
{
    SET_AL(lodsb());
    o8w((uint16_t)(R.di + 0x15), AL);
    if (!(o8((uint16_t)(R.di + 0x0b)) & 0x20)) {
        R.zf = true;
        return;
    }
    SET_AL(o8((uint16_t)(R.di + 9)));
    R.bx = o16((uint16_t)(R.di + 0x0e));
    SET_AL(AL + o8((uint16_t)(R.bx + 8)));
    SET_AL(AL - 0x18);
    Driver1_NoteFreq();
    uint16_t ax = R.ax;
    SET_AH(AL);
    SET_AL(o8((uint16_t)(R.di + 8)) | 0xa0);
    Driver1_OplWriteCached();
    R.ax = ax;
    SET_AL(o8((uint16_t)(R.di + 8)) | 0xb0);
    SET_AH(AH | 0x20);
    o8w((uint16_t)(R.di + 0x0b), AH);
    Driver1_OplWriteCached();
}

/* The other commands (the table at 0327, 90h..9Dh): 94h nothing (0438); 96h n the volume [+11] (0475);
   97h n the drum map flag [+13] (047b); 98h back to the loop mark (0481); 9Ch the loop mark here
   (0486); 9Ah to the stops at 124d (048b); 9Bh one byte skipped (0490); 9Dh two (048f). */
/* Driver1_CmdVolume DRIVER1:0475 */
void Driver1_CmdVolume(void) { SET_AL(lodsb()); o8w((uint16_t)(R.di + 0x11), AL); }
/* Driver1_CmdDrumMap DRIVER1:047b */
void Driver1_CmdDrumMap(void) { SET_AL(lodsb()); o8w((uint16_t)(R.di + 0x13), AL); }
/* Driver1_CmdLoop DRIVER1:0481 */
void Driver1_CmdLoop(void) { R.si = o16((uint16_t)(R.di + 6)); }
/* Driver1_CmdMark DRIVER1:0486 */
void Driver1_CmdMark(void) { o16w((uint16_t)(R.di + 6), R.si); }
/* Driver1_CmdStop DRIVER1:048b */
void Driver1_CmdStop(void) { R.si = 0x124d; }
/* Driver1_CmdSkip2 DRIVER1:048f */
void Driver1_CmdSkip2(void) { SET_AL(lodsb()); SET_AL(lodsb()); }
/* Driver1_CmdSkip1 DRIVER1:0490 */
void Driver1_CmdSkip1(void) { SET_AL(lodsb()); }
/* Driver1_CmdNop DRIVER1:0438 */
void Driver1_CmdNop(void) {}

/* The command table at 0327 is read from the driver's memory, as the original's `call [bx]`. */
static void d1_call(uint16_t target)
{
    switch (target) {
    case 0x0343: Driver1_CmdNoteOffIf(); break;
    case 0x0364: Driver1_CmdEnd(); break;
    case 0x0398: Driver1_CmdInstrument(); break;
    case 0x0430: Driver1_CmdTempo(); break;
    case 0x0438: Driver1_CmdNop(); break;
    case 0x0439: Driver1_CmdPitchBend(); break;
    case 0x0475: Driver1_CmdVolume(); break;
    case 0x047b: Driver1_CmdDrumMap(); break;
    case 0x0481: Driver1_CmdLoop(); break;
    case 0x034a: Driver1_NoteOff(); break;
    case 0x048b: Driver1_CmdStop(); break;
    case 0x0490: Driver1_CmdSkip1(); break;
    case 0x0486: Driver1_CmdMark(); break;
    case 0x048f: Driver1_CmdSkip2(); break;
    default: {
        char m[96];
        snprintf(m, sizeof m, "DRIVER1: command table entry %04x is no routine (the table was overwritten)", target);
        plat_log(m);
    }
    }
}

/* Driver1_VoiceStep DRIVER1:0273
   One voice (DI) at a tick: its delay less the whole ticks gained by its clock (the music's [1267] or
   the effects' [126f]); when it goes below zero, events are read until a delay leaves it at zero or
   above: a note (00..7Fh) is key off (unless an engine effect, 40h and up, sounding: legato), the note
   on with the current instrument, a velocity byte and the volume; FFh only a delay; 80h..8Fh the OPL
   channel (and its operators from the table at 1038h); 90h..9Dh the commands (table at 0327h); above 9Dh
   the end, which also writes 9999h at [0045] (inside engine effect 43h's stream: the original's). */
void Driver1_VoiceStep(void)
{
    SET_AL(o8((uint16_t)(R.di + 0x0a)));
    if (AL == 0) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    R.ax = AL == 2 ? o16(0x126f) : o16(0x1267);
    {
        uint16_t lo = o16((uint16_t)(R.di + 2)), hi = o16((uint16_t)(R.di + 4));
        bool b = lo < R.ax;
        o16w((uint16_t)(R.di + 2), (uint16_t)(lo - R.ax));
        bool b2 = hi < (uint16_t)b;
        hi = (uint16_t)(hi - b);
        o16w((uint16_t)(R.di + 4), hi);
        if (!b2) {
            R.cf = false;
            flags_zs16(hi);
            return;
        }
    }
    R.si = o16(R.di);
    bool delay_only = false;
    for (;;) {
        if (!delay_only) {
            SET_AL(lodsb());
            if (AL & 0x80) {
                if (AL == 0xff) {
                    delay_only = true;
                    continue;
                }
                if (AL > 0x9d) {
                    o16w(0x0045, 0x9999);
                    Driver1_CmdEnd();
                    return;
                }
                bool below = AL < 0x90;
                SET_AL(AL - 0x90);
                if (below) {
                    R.ax &= 0x0f;
                    o8w((uint16_t)(R.di + 8), AL);
                    SET_AL(AL << 1);
                    R.bx = (uint16_t)(0x1038 + R.ax);
                    R.ax = o16(R.bx);
                    o16w((uint16_t)(R.di + 0x0c), R.ax);
                } else {
                    R.bx = 0x0327;
                    SET_AL(AL << 1);
                    SET_AH(0);
                    R.bx = (uint16_t)(R.bx + R.ax);
                    d1_call(o16(R.bx));
                    if (o8((uint16_t)(R.di + 0x0a)) == 0) {
                        R.zf = true;
                        return;
                    }
                }
                delay_only = true;
                continue;
            }
            o8w((uint16_t)(R.di + 9), AL);
            SET_AH(o8((uint16_t)(R.di + 0x0b)));
            if (AH & 0x20) {
                if (!(o8((uint16_t)(R.di + 0x0a)) == 2 && o8((uint16_t)(R.di + 0x12)) >= 0x40)) {
                    SET_AH(AH & 0xdf);
                    o8w((uint16_t)(R.di + 0x0b), AH);
                    SET_AL(o8((uint16_t)(R.di + 8)) | 0xb0);
                    Driver1_OplWriteCached();
                }
            }
            R.bx = o16((uint16_t)(R.di + 0x0e));
            SET_AL(o8((uint16_t)(R.di + 9)));
            Driver1_NoteOn();
            SET_AL(lodsb());
            SET_CL(AL);
            Driver1_SetVolume();
        }
        /* 02d5: the next delay */
        delay_only = false;
        Driver1_ReadDelta();
        uint16_t lo = o16((uint16_t)(R.di + 2)), hi = o16((uint16_t)(R.di + 4));
        uint32_t s = (uint32_t)lo + R.bx;
        o16w((uint16_t)(R.di + 2), (uint16_t)s);
        uint32_t s2 = (uint32_t)hi + R.dx + (s >> 16);
        hi = (uint16_t)s2;
        o16w((uint16_t)(R.di + 4), hi);
        R.cf = s2 > 0xffff;
        flags_zs16(hi);
        if (!(hi & 0x8000)) break;
    }
    o16w(R.di, R.si);
}

/* Driver1_StartTrack DRIVER1:0682
   A music voice (DI) from the next track offset in the tune's header at SI: the stream at [13f0] +
   offset (also the loop mark), its first delay, state 1, bend range 2, no bend, no drum map, no
   instrument (FFh), volume 7Fh. SI past the offset. */
void Driver1_StartTrack(void)
{
    R.ax = lodsw();
    uint16_t si = R.si;
    R.si = (uint16_t)(R.ax + o16(0x13f0));
    o16w((uint16_t)(R.di + 6), R.si);
    Driver1_ReadDelta();
    o16w((uint16_t)(R.di + 2), R.bx);
    o16w((uint16_t)(R.di + 4), R.dx);
    o16w(R.di, R.si);
    o8w((uint16_t)(R.di + 0x0a), 1);
    o8w((uint16_t)(R.di + 0x14), 2);
    o8w((uint16_t)(R.di + 0x15), 0x40);
    o8w((uint16_t)(R.di + 0x13), 0);
    o8w((uint16_t)(R.di + 0x10), 0xff);
    o8w((uint16_t)(R.di + 0x11), 0x7f);
    R.si = si;
}

/* Driver1_StartTune DRIVER1:0606
   At a tick: the tune asked for ([125a], AH = 4) started. The music block at [13f0]: a byte, the number
   of tunes, and a word, the offset of the tune table (words, offsets of the headers); a header is the
   tempo and its multiplier, the number of tracks n, and n track offsets. Every voice is stopped first
   (music and effects). A tune number above the count, a count of 0 or no table: AL = 1. The tracks go
   to voices 0, 1, ... (a count of 0 would start 65536: the loop's). */
void Driver1_StartTune(void)
{
    SET_AL(o8(0x125a));
    if (AL == 0) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    o8w(0x125a, 0);
    R.bx = o16(0x13f0);
    SET_AH(o8(R.bx));
    if (AH == 0 || AL > AH) {
        R.zf = AH == 0, R.cf = false, R.sf = false;
        SET_AL(1);
        return;
    }
    uint16_t ax = R.ax;
    R.cx = 0x10;
    R.di = 0x1271;
    do {
        if (CH != o8((uint16_t)(R.di + 0x0a))) {
            Driver1_NoteOff();
            o8w((uint16_t)(R.di + 0x0a), CH);
            o8w((uint16_t)(R.di + 0x12), CH);
        }
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
    R.ax = ax;
    o8w(0x125b, AL);
    SET_AL(AL - 1);
    R.si = (uint16_t)(o16(0x13f0) + 1);
    R.si = o16(R.si);
    SET_AL(AL << 1);
    SET_AH(0);
    R.si = (uint16_t)(R.si + R.ax);
    if (R.si == R.ax) {
        SET_AL(1);
        R.zf = true;
        return;
    }
    R.si = (uint16_t)(R.si + o16(0x13f0));
    R.si = o16(R.si);
    R.si = (uint16_t)(R.si + o16(0x13f0));
    R.ax = lodsw();
    o16w(0x125e, R.ax);
    SET_AL(lodsb());
    SET_CL(AL);
    SET_CH(0);
    o16w(0x1258, R.cx);
    R.di = 0x1271;
    do {
        uint16_t cx = R.cx;
        Driver1_StartTrack();
        R.cx = cx;
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
    Driver1_CalcRate();
}

/* Driver1_PlayTune DRIVER1:0492
   AH = 4: tune AL asked for (started at the next tick), unless it is the one playing. AL = 0. */
void Driver1_PlayTune(void)
{
    if (o8(0x125b) != AL) o8w(0x125a, AL);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver1_Queue DRIVER1:06cb (06c3 AH = 8 into the stop queue at 13d9, 06c8 AH = 5 into the effects
   queue at 13d1, 06c6 AH = 0Eh into the queue at the caller's SI)
   Effect AL into an 8-byte queue at SI: effects 40h and up (signed: not 80h and up) only once (AL = 0 if
   already there); then the first empty slot; else the first slot holding a lower number (signed),
   replaced; else int 3 (the original's breakpoint, left in) and AL = 1. AL = 0 when queued. */
void Driver1_Queue(void)
{
    R.di = R.si;
    R.cx = 8;
    if ((int8_t)AL >= 0x40) {
        do {
            if (AL == o8(R.si)) {
                SET_AL(0);
                R.zf = true, R.cf = false, R.sf = false;
                return;
            }
            R.si = (uint16_t)(R.si + 1);
        } while (--R.cx);
        R.cx = 8;
        R.si = R.di;
    }
    do {
        if (CH == o8(R.si)) goto put;
        R.si = (uint16_t)(R.si + 1);
    } while (--R.cx);
    R.cx = 8;
    R.si = R.di;
    do {
        SET_AH(o8(R.si));
        if ((int8_t)AH < (int8_t)AL) goto put;
        R.si = (uint16_t)(R.si + 1);
    } while (--R.cx);
    mach_int(3);
    SET_AL(1);
    return;
put:
    o8w(R.si, AL);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver1_StopEffect DRIVER1:06c3 */
void Driver1_StopEffect(void) { R.si = 0x13d9; Driver1_Queue(); }
/* Driver1_PlayEffect DRIVER1:06c8 */
void Driver1_PlayEffect(void) { R.si = 0x13d1; Driver1_Queue(); }
/* Driver1_QueueAtSI DRIVER1:06c6 (AH = 0Eh: the caller's SI, in the driver's segment) */
void Driver1_QueueAtSI(void) { Driver1_Queue(); }

/* Driver1_StopEffectVoiceAt DRIVER1:08ba (Driver1_StopEffectVoice 08b4 from the first voice)
   From the voice DI on, for CX voices: the first effect voice (state 2) playing effect AL is keyed off
   and freed ([+0a] and [+12] := CH, 0 in the normal call). */
void Driver1_StopEffectVoiceAt(void)
{
    SET_AH(2);
    do {
        if (AH == o8((uint16_t)(R.di + 0x0a)) && AL == o8((uint16_t)(R.di + 0x12))) {
            SET_AH(o8((uint16_t)(R.di + 0x0b)));
            if (AH & 0x20) {
                SET_AH(AH & 0xdf);
                o8w((uint16_t)(R.di + 0x0b), AH);
                SET_AL(o8((uint16_t)(R.di + 8)) | 0xb0);
                Driver1_OplWriteCached();
            }
            o8w((uint16_t)(R.di + 0x0a), CH);
            o8w((uint16_t)(R.di + 0x12), CH);
            return;
        }
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
}

/* Driver1_StopEffectVoice DRIVER1:08b4 */
void Driver1_StopEffectVoice(void)
{
    R.di = 0x1271;
    R.cx = 0x10;
    Driver1_StopEffectVoiceAt();
}

/* Driver1_FindFreeVoice DRIVER1:0784
   DI = a free voice (state 0), CF = 0. With none, the original means to take an effect voice playing
   effects 0..7, but compares the state byte (just loaded into CH, so CX is no longer the count) with 7:
   the first voice's state ends up in CH, and the search resumes in the first loop looking for voices in
   that state (1: a music voice is returned as free), or (state 2) runs Driver1_StopEffectVoiceAt from
   the first voice for 200h + n rounds, past the table, and returns the DI it stopped at. Kept as the
   original; a search that would restart for ever (the original hangs in the timer interrupt) gives up
   after the third restart with int 3 and CF = 1 (logged). */
void Driver1_FindFreeVoice(void)
{
    uint16_t ax = R.ax, cx = R.cx;
    int restarts = 0;
    R.di = 0x1271;
    R.cx = 0x10;
l078c:
    if (CH == o8((uint16_t)(R.di + 0x0a))) goto found;
    R.di = (uint16_t)(R.di + 0x16);
    if (--R.cx) goto l078c;
    if (++restarts > 3) {
        plat_log("DRIVER1: the voice search restarts for ever (the original hangs here); given up");
        goto none;
    }
    R.di = 0x1271;   /* 0798 */
    R.cx = 0x10;
    SET_CH(o8((uint16_t)(R.di + 0x0a)));
    if (CH == 2 && (int8_t)CH <= 7) {
        Driver1_StopEffectVoiceAt();
        goto found;
    }
    R.di = (uint16_t)(R.di + 0x16);
    if (--R.cx) goto l078c;
none:
    R.cx = cx, R.ax = ax;
    mach_int(3);
    R.cf = true;
    return;
found:
    R.cx = cx, R.ax = ax;
    R.cf = false;
}

/* Driver1_FindFreeChannel DRIVER1:07be
   BL = an OPL channel (8 down to 0) no active voice uses, CF = 0. With all nine in use: if an effect
   voice is playing an effect 0..7, BL = FFh (the count run out: the original's) and CF = 0; else int 3,
   CF = 1. AX, CX, DI kept. */
void Driver1_FindFreeChannel(void)
{
    uint16_t ax = R.ax, cx = R.cx, di = R.di;
    SET_BL(8);
    for (;;) {
        R.di = 0x1271;
        R.cx = 0x10;
        bool used = false;
        do {
            if (CH != o8((uint16_t)(R.di + 0x0a)) && BL == o8((uint16_t)(R.di + 8))) { used = true; break; }
            R.di = (uint16_t)(R.di + 0x16);
        } while (--R.cx);
        if (!used) goto done;
        SET_BL(BL - 1);
        if (BL & 0x80) break;
    }
    R.di = 0x1271;
    R.cx = 0x10;
    do {
        if (o8((uint16_t)(R.di + 0x0a)) == 2 && (int8_t)o8((uint16_t)(R.di + 0x12)) <= 7) goto done;
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
    R.di = di, R.cx = cx, R.ax = ax;
    mach_int(3);
    R.cf = true;
    return;
done:
    R.di = di, R.cx = cx, R.ax = ax;
    R.cf = false;
}

/* Driver1_StartEffect DRIVER1:0722
   Effect AL on a free voice and channel: effects below 40h (signed) from the effects block at [13f2]
   (the word table after its first byte: offsets from the block), 40h and up the 16-byte streams at
   0008 + 10h * (AL - 40h), the engine notes the game writes there. State 2, the effect number in [+12].
   CF = 1 when no voice or channel was found. SI kept. */
void Driver1_StartEffect(void)
{
    Driver1_FindFreeVoice();
    if (R.cf) return;
    Driver1_FindFreeChannel();
    if (R.cf) return;
    o8w((uint16_t)(R.di + 8), BL);
    o8w((uint16_t)(R.di + 0x12), AL);
    SET_BH(0);
    SET_BL(BL << 1);
    R.bx = (uint16_t)(R.bx + 0x1038);
    R.bx = o16(R.bx);
    o16w((uint16_t)(R.di + 0x0c), R.bx);
    uint16_t si = R.si;
    if ((int8_t)AL >= 0x40) {
        SET_BL(AL - 0x40);
        SET_BH(0);
        R.bx = (uint16_t)(R.bx << 4);
        R.bx = (uint16_t)(R.bx + 8);
        R.si = R.bx;
    } else {
        SET_AL(AL - 1);
        SET_AL(AL << 1);
        SET_AH(0);
        R.si = o16(0x13f2);
        R.bx = R.si;
        R.si = (uint16_t)(R.si + 1 + R.ax);
        R.si = o16(R.si);
        R.si = (uint16_t)(R.si + R.bx);
    }
    Driver1_ReadDelta();
    o16w((uint16_t)(R.di + 2), R.bx);
    o16w((uint16_t)(R.di + 4), R.dx);
    o16w(R.di, R.si);
    o8w((uint16_t)(R.di + 0x0a), 2);
    R.si = si;
    R.cf = false;
}

/* Driver1_StartEffects DRIVER1:0704
   At a tick: each queued effect (13d1, 8 slots, emptied as taken) started; stops at the first that
   finds no voice or channel (the rest wait for the next tick). */
void Driver1_StartEffects(void)
{
    R.si = 0x13d1;
    R.cx = 8;
    do {
        uint16_t cx = R.cx;
        SET_AL(lodsb());
        if (AL != 0) {
            o8w((uint16_t)(R.si - 1), CH);
            R.bx = o16(0x13f2);
            Driver1_StartEffect();
            if (R.cf) {
                R.cx = cx;
                return;
            }
        }
        R.cx = cx;
    } while (--R.cx);
}

/* Driver1_StopEffects DRIVER1:089e
   At a tick: each effect in the stop queue (13d9) stopped on the first voice playing it. */
void Driver1_StopEffects(void)
{
    R.si = 0x13d9;
    R.cx = 8;
    do {
        uint16_t cx = R.cx;
        SET_AL(lodsb());
        if (AL != 0) {
            o8w((uint16_t)(R.si - 1), CH);
            Driver1_StopEffectVoice();
        }
        R.cx = cx;
    } while (--R.cx);
}

/* Driver1_ClearQueues DRIVER1:088f
   Both queues (13d1..13e0) emptied. */
void Driver1_ClearQueues(void)
{
    R.di = 0x13d1;
    R.cx = 8;
    R.ax = 0;
    for (; R.cx; R.cx--, R.di = (uint16_t)(R.di + 2)) o16w(R.di, R.ax);
}

/* Driver1_DoStopAll DRIVER1:080d
   At a tick after AH = 6: the queues emptied, the register list at 104Ah written (words, register in
   the low byte, value in the high, up to a zero word: all levels to 3Fh, sustain/release and attack/
   decay to FFh, B0h..B9h to 0, key off), every voice freed, no tune playing. */
void Driver1_DoStopAll(void)
{
    if (o8(0x125d) == 0) {
        R.zf = true, R.cf = false, R.sf = false;
        return;
    }
    Driver1_ClearQueues();
    R.si = 0x104a;
    for (;;) {
        R.ax = lodsw();
        if (R.ax == 0) break;
        Driver1_OplWrite();
    }
    R.di = 0x1271;
    R.cx = 0x10;
    do {
        o8w((uint16_t)(R.di + 0x0a), CH);
        o8w((uint16_t)(R.di + 0x12), CH);
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
    o8w(0x125d, CL);
    o8w(0x125b, CL);
}

/* Driver1_StopAll DRIVER1:0804
   AH = 6: everything stopped at the next tick. AL = 0. */
void Driver1_StopAll(void)
{
    o8w(0x125d, 1);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver1_DoStopMusic DRIVER1:084a
   At a tick after AH = 7: the first [1258] voices (the tune's tracks) keyed off and freed, no tune
   playing. With a track count of 0 or above 16 the request stays (and is acted on once a tune starts:
   the original's). */
void Driver1_DoStopMusic(void)
{
    if (o8(0x125c) == 0) {
        R.zf = true, R.cf = false, R.sf = false;
        return;
    }
    R.cx = o16(0x1258);
    if (R.cx == 0 || R.cx > 0x10) return;
    R.di = 0x1271;
    do {
        SET_AH(o8((uint16_t)(R.di + 0x0b)));
        if (AH & 0x20) {
            SET_AH(AH & 0xdf);
            o8w((uint16_t)(R.di + 0x0b), AH);
            SET_AL(o8((uint16_t)(R.di + 8)) | 0xb0);
            Driver1_OplWriteCached();
        }
        o8w((uint16_t)(R.di + 0x0a), CH);
        o8w((uint16_t)(R.di + 0x12), CH);
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
    o8w(0x125c, CL);
    o8w(0x125b, CL);
}

/* Driver1_StopMusic DRIVER1:0841
   AH = 7: the music stopped at the next tick. AL = 0. */
void Driver1_StopMusic(void)
{
    o8w(0x125c, 1);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver1_TuneQuery DRIVER1:08f1
   AH = 9: AL = AL - the tune playing (0: tune AL is playing; the game then skips AH = 4). */
void Driver1_TuneQuery(void)
{
    uint8_t t = o8(0x125b);
    R.cf = AL < t;
    SET_AL(AL - t);
    flags_zs8(AL);
}

/* Driver1_EffectQuery DRIVER1:08f6
   AH = 0Ah: AL = 0 if a voice is playing effect AL, else AL unchanged (AH = AL). */
void Driver1_EffectQuery(void)
{
    R.di = 0x1271;
    R.cx = 0x10;
    SET_AH(AL);
    do {
        uint8_t e = o8((uint16_t)(R.di + 0x12));
        R.cf = AL < e;
        SET_AL(AL - e);
        flags_zs8(AL);
        if (AL == 0) return;
        SET_AL(AH);
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
}

/* Driver1_CountEffects DRIVER1:090d
   AH = 0Bh: AX = 0200h + the number of effect voices (state 2). */
void Driver1_CountEffects(void)
{
    R.di = 0x1271;
    R.cx = 0x10;
    R.ax = 0x0200;
    do {
        if (AH == o8((uint16_t)(R.di + 0x0a))) SET_AL(AL + 1);
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
}

/* Driver1_Tick DRIVER1:021a
   AH = 3, once per frame from the game's timer handler: unless a tick is running (AX = FFFEh), the
   requests of the calls since the last tick (stop all, stop the music, the tune, the effects to stop,
   the effects to start), then the two clocks advanced by their steps and every voice stepped by the
   whole ticks of its clock, which are then taken off (the fractions stay). */
void Driver1_Tick(void)
{
    if (o8(0x1260) != 0) {
        R.ax = 0xfffe;
        R.cf = false, R.zf = false, R.sf = false;
        return;
    }
    o8w(0x1260, (uint8_t)(o8(0x1260) + 1));
    Driver1_DoStopAll();
    Driver1_DoStopMusic();
    Driver1_StartTune();
    Driver1_StopEffects();
    Driver1_StartEffects();
    for (uint16_t si = 0x1261; si <= 0x1269; si = (uint16_t)(si + 8)) {
        R.si = si;
        R.ax = o16(si);
        uint32_t s = (uint32_t)o16((uint16_t)(si + 4)) + R.ax;
        o16w((uint16_t)(si + 4), (uint16_t)s);
        R.ax = o16((uint16_t)(si + 2));
        o16w((uint16_t)(si + 6), (uint16_t)(o16((uint16_t)(si + 6)) + R.ax + (s >> 16)));
    }
    R.di = 0x1271;
    R.cx = 0x10;
    do {
        uint16_t cx = R.cx;
        Driver1_VoiceStep();
        R.cx = cx;
        R.di = (uint16_t)(R.di + 0x16);
    } while (--R.cx);
    o16w(0x1267, R.cx);
    o16w(0x126f, R.cx);
    SET_AL(0);
    o8w(0x1260, AL);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver1_Dispatch DRIVER1:0094 (from Driver1_Entry DRIVER1:0000, the far entry, and 0004, the same as
   an interrupt handler)
   The function AH through the table at 00C4 if AH <= [00c3] (0 until AH = 0 has run, 10h after), else
   AX = FFFFh. BX, CX, DX, SI, DI, DS and the flags come back as they were; BP and ES as the function
   leaves them (none changes them). The table has 12 routines (AH = 0..0Bh); entries 0Ch..10h are the
   bytes of the code after it, words 1689h, 1252h (data), 06C6h (inside AH = 8: a queue at the caller's
   SI) and 00C3h, 3210h (data): the game never calls them with this driver (Snd_* test the device). */
void Driver1_Dispatch(void)
{
    Cpu saved = R;
    R.ds = SEG_OVL;
    R.df = false;
    if (AH > o8(0x00c3)) {
        R = saved;
        R.ax = 0xffff;
        return;
    }
    uint16_t target = o16((uint16_t)(0x00c4 + (uint8_t)(AH << 1)));
    switch (target) {
    case 0x00e8: Driver1_Init(); break;
    case 0x0203: Driver1_Shutdown(); break;
    case 0x020f: Driver1_SetRate(); break;
    case 0x021a: Driver1_Tick(); break;
    case 0x0492: Driver1_PlayTune(); break;
    case 0x06c8: Driver1_PlayEffect(); break;
    case 0x0804: Driver1_StopAll(); break;
    case 0x0841: Driver1_StopMusic(); break;
    case 0x06c3: Driver1_StopEffect(); break;
    case 0x08f1: Driver1_TuneQuery(); break;
    case 0x08f6: Driver1_EffectQuery(); break;
    case 0x090d: Driver1_CountEffects(); break;
    case 0x06c6: Driver1_QueueAtSI(); break;
    default: {
        /* a jump into the driver's data: what the CPU would run there is not ported */
        char m[96];
        snprintf(m, sizeof m, "DRIVER1: function %02xh jumps to %04x (data); not run", AH, target);
        plat_log(m);
    }
    }
    uint16_t ax = R.ax, bp = R.bp, es = R.es;
    R = saved;
    R.ax = ax, R.bp = bp, R.es = es;
}

/* Driver1_Entry DRIVER1:0000 */
void Driver1_Entry(void) { Driver1_Dispatch(); }
