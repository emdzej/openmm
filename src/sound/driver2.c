/* DRIVER2.BIN, the internal beeper driver (the same package as DRIVER1.BIN, its PC-speaker version),
   ported routine by routine (docs/re/sound.md). The same design as
   the AdLib driver (driver1.c) on one square wave: four voices of 12h bytes at 009F step their streams
   at each tick, and the speaker (PIT channel 2, port 61h) plays one of them per tick, taken in turn
   among those sounding (a fast arpeggio of the chord); the game's two engine notes (AH = 0Eh..10h)
   alternate tick by tick instead while no effect is playing. Code 0000-0688, the tone table at 0689,
   then music, effects and instruments ([0749], [074b], [074d]).

   Voice (DI = 009F + 12h * n): +0 the stream pointer, +2/+4 the delay left, +6 the loop mark, +8 the
   channel byte of commands 80h..8Fh (not used for sound), +9 the note, +0a the state (0 free, 1 music,
   2 effect), +0b sounding, +0c the instrument, +0e the effect number, +0f the PIT count of the note,
   +11 the ticks left of the note (the instrument's +9; 0 = until the next).

   Variables: 0073 the rate (AH = 2's CX), 0075 the number of tracks, 0077 the tune asked for, 0078 the
   tune playing, 0079/007a stop the music / all at the next tick, 007b the tempo and 007c its multiplier,
   007d busy, 007e..0085 the music clock (step, value), 0086..008d the effects' clock, 008e the count
   last written to the PIT, 0090 the turn counter and 0091 the four voices' addresses, 0099 the engines'
   alternation, 009a the engines on (bit 0 engine 0, bit 1 engine 1), 009b/009d their PIT counts, 00e7
   the effects queue (8), 00ef the queue of effects to stop (8), 0124 the highest function accepted. */
#include "sound.h"
#include "machine.h"
#include "pending.h"
#include <stdio.h>

void Driver2_CalcRate(void);
void Driver2_ReadDelta(void);
void Driver2_SetTone(void);
void Driver2_NoteOff(void);

/* Driver2_CalcRate DRIVER2:0603
   As the AdLib driver's (driver1.c, Driver1_CalcRate): n = 444_6390h / rate [0073]; the music's step
   [0080]:[007e] = tempo [007b] * multiplier [007c] / n, the effects' [0088]:[0086] = 1680h / n; a rate
   below 445h: [0080] = 0, [007e] = 1, AL = 1. */
void Driver2_CalcRate(void)
{
    R.dx = 0x0444;
    R.ax = 0x6390;
    R.cx = o16(0x0073);
    if (R.cx < 0x445) {
        R.ax = 0;
        o16w(0x0080, R.ax);
        R.ax = 1;
        o16w(0x007e, R.ax);
        R.cf = true, R.zf = false, R.sf = false;
        return;
    }
    uint32_t n = (uint32_t)R.dx << 16 | R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    R.cx = R.ax;
    R.ax = (uint16_t)(o8(0x007b) * o8(0x007c));
    R.dx = 0;
    n = R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x0080, R.ax);
    n = (uint32_t)R.dx << 16 | R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x007e, R.ax);
    R.ax = 0x1680;
    R.dx = 0;
    n = R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x0088, R.ax);
    n = (uint32_t)R.dx << 16 | R.ax;
    R.ax = (uint16_t)(n / R.cx), R.dx = (uint16_t)(n % R.cx);
    o16w(0x0086, R.ax);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_Init DRIVER2:0147
   AH = 0: channel 2 to mode 3 (lobyte/hibyte), the voices freed and numbered, the clocks' steps, the
   functions allowed ([0124] := 10h). AL = 0. (No port: DX is not read.) */
void Driver2_Init(void)
{
    io_out8(0x43, 0xb6);
    R.di = 0x009f;
    R.cx = 4;
    SET_AL(0);
    do {
        o8w((uint16_t)(R.di + 0x0a), CH);
        o8w((uint16_t)(R.di + 0x0e), CH);
        o8w((uint16_t)(R.di + 8), AL);
        SET_AL(AL + 1);
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
    Driver2_CalcRate();
    o8w(0x0124, 0x10);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_Shutdown DRIVER2:016e
   AH = 1: the speaker off (port 61h bits 0, 1), the functions refused from then on. AL = 0. */
void Driver2_Shutdown(void)
{
    uint8_t al = (uint8_t)(io_in8(0x61) & 0xfc);
    io_out8(0x61, al);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
    o8w(0x0124, 0);
}

/* Driver2_SetRate DRIVER2:017a
   AH = 2: the rate [0073] := CX, the steps recomputed with interrupts off, the flags kept. */
void Driver2_SetRate(void)
{
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    o16w(0x0073, R.cx);
    Driver2_CalcRate();
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Driver2_ReadDelta DRIVER2:0599
   A delay from the stream at SI into DX:BX, as Driver1_ReadDelta (driver1.c). */
void Driver2_ReadDelta(void)
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
        if (!(AL & 0x80)) {
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
    R.cf = false, R.zf = false, R.sf = true;
}

/* Driver2_SetTone DRIVER2:02dd
   Note AL (already transposed by the instrument's +8) on the voice: n = AL - 18h, none above 5Fh; the
   PIT count from the table at 0689h (60h words) into [+0f], and the voice sounding only if the
   instrument BX gives the note a length (+9, into [+11]). CX, SI kept. */
void Driver2_SetTone(void)
{
    SET_AL(AL - 0x18);
    if (AL > 0x5f) {
        R.cf = false, R.zf = false;
        return;
    }
    uint16_t cx = R.cx, si = R.si;
    SET_AL(AL << 1);
    SET_AH(0);
    o8w((uint16_t)(R.di + 0x0b), AH);
    R.si = (uint16_t)(0x0689 + R.ax);
    R.ax = lodsw();
    o16w((uint16_t)(R.di + 0x0f), R.ax);
    SET_AL(o8((uint16_t)(R.bx + 9)));
    if (AL != 0) {
        o8w((uint16_t)(R.di + 0x11), AL);
        SET_AL(1);
        o8w((uint16_t)(R.di + 0x0b), AL);
    }
    R.si = si, R.cx = cx;
    R.zf = AL == 0;
}

/* Driver2_NoteOff DRIVER2:027f
   Command 99h: the voice silent (note, sounding and length cleared). */
void Driver2_NoteOff(void)
{
    SET_AL(0);
    o8w((uint16_t)(R.di + 9), AL);
    o8w((uint16_t)(R.di + 0x0b), AL);
    o8w((uint16_t)(R.di + 0x11), AL);
}

/* Driver2_CmdNoteOffIf DRIVER2:0279
   Command 90h n: silent if the voice's note is n. */
void Driver2_CmdNoteOffIf(void)
{
    SET_AL(lodsb());
    uint8_t n = o8((uint16_t)(R.di + 9));
    R.cf = AL < n;
    SET_AL(AL - n);
    flags_zs8(AL);
    if (AL != 0) return;
    Driver2_NoteOff();
}

/* Driver2_CmdEnd DRIVER2:028b
   Command 91h (and any byte above 9Dh): the voice ends; the last music voice leaves no tune playing. */
void Driver2_CmdEnd(void)
{
    SET_AL(o8((uint16_t)(R.di + 0x0a)));
    o8w((uint16_t)(R.di + 0x0a), 0);
    o8w((uint16_t)(R.di + 0x0e), 0);
    if (AL == 1) {
        uint16_t cx = R.cx, di = R.di;
        R.di = 0x009f;
        R.cx = 4;
        bool found = false;
        do {
            if (AL == o8((uint16_t)(R.di + 0x0a))) { found = true; break; }
            R.di = (uint16_t)(R.di + 0x12);
        } while (--R.cx);
        if (!found) o8w(0x0078, 0);
        R.di = di, R.cx = cx;
    }
    Driver2_NoteOff();
}

/* Driver2_CmdInstrument DRIVER2:02b5
   Command 92h n: the instrument [074d] + 10h * n (CL = 4 left: the shift count). */
void Driver2_CmdInstrument(void)
{
    SET_AL(lodsb());
    SET_AH(0);
    SET_CL(4);
    R.ax = (uint16_t)(R.ax << 4);
    R.ax = (uint16_t)(R.ax + o16(0x074d));
    o16w((uint16_t)(R.di + 0x0c), R.ax);
}

/* Driver2_CmdTempo DRIVER2:02c4
   Command 93h n: the tempo multiplier [007c] := n, the steps recomputed. */
void Driver2_CmdTempo(void)
{
    SET_AL(lodsb());
    o8w(0x007c, AL);
    Driver2_CalcRate();
}

/* The other commands (table at 025D): 94h..97h one byte skipped (02cc: the AdLib's nothing, pitch
   bend, volume and drum map have no meaning on the beeper), 98h back to the loop mark (02ce), 9Ch the
   mark here (02d2), 9Ah to the stops at 006E (02d6), 9Bh one byte skipped (02db), 9Dh two (02da). */
/* Driver2_CmdSkip1 DRIVER2:02cc */
void Driver2_CmdSkip1(void) { SET_AL(lodsb()); }
/* Driver2_CmdLoop DRIVER2:02ce */
void Driver2_CmdLoop(void) { R.si = o16((uint16_t)(R.di + 6)); }
/* Driver2_CmdMark DRIVER2:02d2 */
void Driver2_CmdMark(void) { o16w((uint16_t)(R.di + 6), R.si); }
/* Driver2_CmdStop DRIVER2:02d6 */
void Driver2_CmdStop(void) { R.si = 0x006e; }
/* Driver2_CmdSkip2 DRIVER2:02da */
void Driver2_CmdSkip2(void) { SET_AL(lodsb()); SET_AL(lodsb()); }
/* Driver2_CmdSkipOne DRIVER2:02db */
void Driver2_CmdSkipOne(void) { SET_AL(lodsb()); }

static void d2_call(uint16_t target)
{
    switch (target) {
    case 0x0279: Driver2_CmdNoteOffIf(); break;
    case 0x028b: Driver2_CmdEnd(); break;
    case 0x02b5: Driver2_CmdInstrument(); break;
    case 0x02c4: Driver2_CmdTempo(); break;
    case 0x02cc: Driver2_CmdSkip1(); break;
    case 0x02ce: Driver2_CmdLoop(); break;
    case 0x027f: Driver2_NoteOff(); break;
    case 0x02d6: Driver2_CmdStop(); break;
    case 0x02db: Driver2_CmdSkipOne(); break;
    case 0x02d2: Driver2_CmdMark(); break;
    case 0x02da: Driver2_CmdSkip2(); break;
    default: {
        char m[96];
        snprintf(m, sizeof m, "DRIVER2: command table entry %04x is no routine (the table was overwritten)", target);
        plat_log(m);
    }
    }
}

/* Driver2_VoiceStep DRIVER2:01e0
   One voice at a tick: the note's length counted down (silent at 0), then as the AdLib's voice step: the
   delay less the clock's whole ticks, and events read while it is below zero: a note (its tone, the
   velocity byte skipped, the length from the instrument), FFh a delay, 80h..8Fh the channel byte,
   90h..9Dh the commands, above 9Dh the end (with AH = 99h, unused). */
void Driver2_VoiceStep(void)
{
    SET_AL(0);
    if (AL == o8((uint16_t)(R.di + 0x0a))) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    if (AL != o8((uint16_t)(R.di + 0x11))) {
        uint8_t n = (uint8_t)(o8((uint16_t)(R.di + 0x11)) - 1);
        o8w((uint16_t)(R.di + 0x11), n);
        if (n == 0) o8w((uint16_t)(R.di + 0x0b), AL);
    }
    SET_AL(o8((uint16_t)(R.di + 0x0a)));
    R.ax = AL == 2 ? o16(0x008c) : o16(0x0084);
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
                    SET_AH(0x99);
                    Driver2_CmdEnd();
                    return;
                }
                bool below = AL < 0x90;
                SET_AL(AL - 0x90);
                if (below) {
                    R.ax &= 0x0f;
                    o8w((uint16_t)(R.di + 8), AL);
                } else {
                    SET_BL(AL << 1);
                    SET_BH(0);
                    d2_call(o16((uint16_t)(R.bx + 0x025d)));
                    if (o8((uint16_t)(R.di + 0x0a)) == 0) {
                        R.zf = true;
                        return;
                    }
                }
                delay_only = true;
                continue;
            }
            o8w((uint16_t)(R.di + 9), AL);
            R.bx = o16((uint16_t)(R.di + 0x0c));
            SET_AL(AL + o8((uint16_t)(R.bx + 8)));
            Driver2_SetTone();
            SET_AL(lodsb());
            SET_AL(o8((uint16_t)(R.bx + 9)));
            o8w((uint16_t)(R.di + 0x11), AL);
        }
        delay_only = false;
        Driver2_ReadDelta();
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

/* Driver2_SpeakerOut DRIVER2:0307
   At the end of a tick, what the speaker plays: with an engine on and no effect playing, the engines
   in turn ([0099] odd: engine 1, even: engine 0; an engine off on its turn: silence); else, if a voice
   is sounding, the next sounding voice after the last ([0090] counts the turns); else silence. A count
   is written to the PIT only when it changed (low byte, then the high byte's low 5 bits), the gate and
   the speaker on (port 61h bits 0, 1); silence clears the gate. */
void Driver2_SpeakerOut(void)
{
    if (o8(0x009a) != 0) {
        SET_AL(2);
        if (AL != o8(0x00a9) && AL != o8(0x00bb) && AL != o8(0x00cd) && AL != o8(0x00df)) {
            o8w(0x0099, (uint8_t)(o8(0x0099) + 1));
            if (o8(0x0099) & 1) {
                if (!(o8(0x009a) & 2)) goto off;
                R.ax = o16(0x009d);
            } else {
                if (!(o8(0x009a) & 1)) goto off;
                R.ax = o16(0x009b);
            }
            goto play;
        }
    }
    SET_AL(o8(0x00aa) | o8(0x00bc) | o8(0x00ce) | o8(0x00e0));
    if (AL == 0) goto off;
    for (;;) {
        R.si = 0x0090;
        o8w(R.si, (uint8_t)(o8(R.si) + 1));
        SET_AL(lodsb());
        R.ax &= 3;
        SET_AL(AL << 1);
        R.si = (uint16_t)(R.si + R.ax);
        R.si = o16(R.si);
        if (AH != o8((uint16_t)(R.si + 0x0b))) break;
    }
    R.ax = o16((uint16_t)(R.si + 0x0f));
play:
    if (R.ax != o16(0x008e)) {
        o16w(0x008e, R.ax);
        io_out8(0x42, AL);
        SET_AL(AH & 0x1f);
        io_out8(0x42, AL);
    }
    SET_AL(io_in8(0x61) | 3);
    io_out8(0x61, AL);
    R.cf = false;
    flags_zs8(AL);
    return;
off:
    SET_AL(io_in8(0x61) & 0xfe);
    io_out8(0x61, AL);
    R.cf = false;
    flags_zs8(AL);
}

/* Driver2_PlayTune DRIVER2:0392
   AH = 4: tune AL asked for, started at the next tick (even if it is playing). AL = 0. */
void Driver2_PlayTune(void)
{
    o8w(0x0077, AL);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_StartTrack DRIVER2:0410
   A music voice from the next track offset at SI (offsets from [0749]); state 1. */
void Driver2_StartTrack(void)
{
    R.ax = lodsw();
    uint16_t si = R.si;
    R.si = (uint16_t)(R.ax + o16(0x0749));
    o16w((uint16_t)(R.di + 6), R.si);
    Driver2_ReadDelta();
    o16w((uint16_t)(R.di + 2), R.bx);
    o16w((uint16_t)(R.di + 4), R.dx);
    o16w(R.di, R.si);
    o8w((uint16_t)(R.di + 0x0a), 1);
    R.si = si;
}

/* Driver2_StartTune DRIVER2:0398
   At a tick: the tune asked for ([0077]) from the music block at [0749] (the AdLib's layout,
   Driver1_StartTune): every voice freed, then at most four tracks started (the track count is kept in
   [0075] whole). AL = 1 for a tune that is not there. */
void Driver2_StartTune(void)
{
    SET_AL(o8(0x0077));
    if (AL == 0) {
        R.cf = false, R.zf = true, R.sf = false;
        return;
    }
    o8w(0x0077, 0);
    R.bx = o16(0x0749);
    SET_AH(o8(R.bx));
    if (AH == 0 || AL > AH) {
        R.zf = AH == 0, R.cf = false, R.sf = false;
        SET_AL(1);
        return;
    }
    uint16_t ax = R.ax;
    R.di = 0x009f;
    R.cx = 4;
    do {
        o8w((uint16_t)(R.di + 0x0a), CH);
        o8w((uint16_t)(R.di + 0x0e), CH);
        o8w((uint16_t)(R.di + 0x0b), CH);
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
    R.ax = ax;
    o8w(0x0078, AL);
    SET_AL(AL - 1);
    R.si = (uint16_t)(o16(0x0749) + 1);
    R.si = o16(R.si);
    SET_AL(AL << 1);
    SET_AH(0);
    R.si = (uint16_t)(R.si + R.ax);
    if (R.si == R.ax) {
        SET_AL(1);
        R.zf = true;
        return;
    }
    R.si = (uint16_t)(R.si + o16(0x0749));
    R.si = o16(R.si);
    R.si = (uint16_t)(R.si + o16(0x0749));
    R.ax = lodsw();
    o16w(0x007b, R.ax);
    SET_AL(lodsb());
    SET_CL(AL);
    SET_CH(0);
    o16w(0x0075, R.cx);
    R.di = 0x009f;
    if (CL > 4) SET_CL(4);
    do {   /* a count of 0 would start 65536 tracks: the loop's */
        uint16_t cx = R.cx;
        Driver2_StartTrack();
        R.cx = cx;
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
    Driver2_CalcRate();
}

/* Driver2_Queue DRIVER2:0434 (042c AH = 8 the stop queue at 00EF, 0431 AH = 5 the effects queue at
   00E7)
   Effect AL into the first empty slot of the queue; AL = 1 when it is full. */
void Driver2_Queue(void)
{
    R.cx = 8;
    do {
        if (CH == o8(R.si)) {
            o8w(R.si, AL);
            SET_AL(0);
            R.cf = false, R.zf = true, R.sf = false;
            return;
        }
        R.si = (uint16_t)(R.si + 1);
    } while (--R.cx);
    SET_AL(1);
}

/* Driver2_StopEffect DRIVER2:042c */
void Driver2_StopEffect(void) { R.si = 0x00ef; Driver2_Queue(); }
/* Driver2_PlayEffect DRIVER2:0431 */
void Driver2_PlayEffect(void) { R.si = 0x00e7; Driver2_Queue(); }

/* Driver2_FindFreeVoice DRIVER2:0499
   DI = a free voice, CF = 0; CF = 1 when all four are busy. AX, CX kept. */
void Driver2_FindFreeVoice(void)
{
    uint16_t ax = R.ax, cx = R.cx;
    R.di = 0x009f;
    R.cx = 4;
    do {
        if (CH == o8((uint16_t)(R.di + 0x0a))) {
            R.cx = cx, R.ax = ax;
            R.cf = false;
            return;
        }
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
    R.cx = cx, R.ax = ax;
    R.cf = true;
}

/* Driver2_StartEffect DRIVER2:0468
   Effect AL on a free voice: the stream from the effects block at [074b] (the word table after its
   first byte), state 2. CF = 1 when no voice is free. SI kept. */
void Driver2_StartEffect(void)
{
    Driver2_FindFreeVoice();
    if (R.cf) return;
    o8w((uint16_t)(R.di + 0x0e), AL);
    SET_AL(AL - 1);
    SET_AL(AL << 1);
    SET_AH(0);
    uint16_t si = R.si;
    R.si = o16(0x074b);
    R.bx = R.si;
    R.si = (uint16_t)(R.si + 1 + R.ax);
    R.si = o16(R.si);
    R.si = (uint16_t)(R.si + R.bx);
    o16w((uint16_t)(R.di + 6), R.si);
    Driver2_ReadDelta();
    o16w((uint16_t)(R.di + 2), R.bx);
    o16w((uint16_t)(R.di + 4), R.dx);
    o16w(R.di, R.si);
    o8w((uint16_t)(R.di + 0x0a), 2);
    R.si = si;
    R.cf = false;
}

/* Driver2_StartEffects DRIVER2:0446
   At a tick: each queued effect (emptied as taken) that is in the effects block (not above its count,
   the block's first byte) started; stops at the first that finds no voice. */
void Driver2_StartEffects(void)
{
    R.si = 0x00e7;
    R.cx = 8;
    do {
        uint16_t cx = R.cx;
        SET_AL(lodsb());
        if (AL != 0) {
            o8w((uint16_t)(R.si - 1), CH);
            R.bx = o16(0x074b);
            if (!(AL > o8(R.bx))) {
                Driver2_StartEffect();
                if (R.cf) {
                    R.cx = cx;
                    return;
                }
            }
        }
        R.cx = cx;
    } while (--R.cx);
}

/* Driver2_ClearQueues DRIVER2:051a */
void Driver2_ClearQueues(void)
{
    R.di = 0x00e7;
    R.cx = 8;
    R.ax = 0;
    for (; R.cx; R.cx--, R.di = (uint16_t)(R.di + 2)) o16w(R.di, R.ax);
}

/* Driver2_DoStopAll DRIVER2:04bb
   At a tick after AH = 6: the queues emptied, every voice freed, no tune, the speaker off. */
void Driver2_DoStopAll(void)
{
    if (o8(0x007a) == 0) {
        R.zf = true, R.cf = false, R.sf = false;
        return;
    }
    Driver2_ClearQueues();
    R.di = 0x009f;
    R.cx = 4;
    do {
        o8w((uint16_t)(R.di + 0x0b), CH);
        o8w((uint16_t)(R.di + 0x0a), CH);
        o8w((uint16_t)(R.di + 0x0e), CH);
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
    o8w(0x007a, CL);
    o8w(0x0078, CL);
    SET_AL(io_in8(0x61) & 0xfc);
    io_out8(0x61, AL);
    R.cf = false;
    flags_zs8(AL);
}

/* Driver2_StopAll DRIVER2:04b3 */
void Driver2_StopAll(void)
{
    o8w(0x007a, 1);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_DoStopMusic DRIVER2:04f0
   At a tick after AH = 7: every voice freed (the effects' too, unlike the AdLib's), no tune, the
   speaker off. */
void Driver2_DoStopMusic(void)
{
    if (o8(0x0079) == 0) {
        R.zf = true, R.cf = false, R.sf = false;
        return;
    }
    R.cx = 4;
    R.di = 0x009f;
    do {
        o8w((uint16_t)(R.di + 0x0b), CH);
        o8w((uint16_t)(R.di + 0x0a), CH);
        o8w((uint16_t)(R.di + 0x0e), CH);
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
    o8w(0x0079, CL);
    o8w(0x0078, CL);
    SET_AL(io_in8(0x61) & 0xfc);
    io_out8(0x61, AL);
    R.cf = false;
    flags_zs8(AL);
}

/* Driver2_StopMusic DRIVER2:04e8 */
void Driver2_StopMusic(void)
{
    o8w(0x0079, 1);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_StopEffectVoice DRIVER2:053f
   The first effect voice playing effect AL freed. */
void Driver2_StopEffectVoice(void)
{
    R.di = 0x009f;
    R.cx = 4;
    SET_AH(2);
    do {
        if (AH == o8((uint16_t)(R.di + 0x0a)) && AL == o8((uint16_t)(R.di + 0x0e))) {
            o8w((uint16_t)(R.di + 0x0b), CH);
            o8w((uint16_t)(R.di + 0x0a), CH);
            o8w((uint16_t)(R.di + 0x0e), CH);
            return;
        }
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
}

/* Driver2_StopEffects DRIVER2:0529 */
void Driver2_StopEffects(void)
{
    R.si = 0x00ef;
    R.cx = 8;
    do {
        uint16_t cx = R.cx;
        SET_AL(lodsb());
        if (AL != 0) {
            o8w((uint16_t)(R.si - 1), CH);
            Driver2_StopEffectVoice();
        }
        R.cx = cx;
    } while (--R.cx);
}

/* Driver2_TuneQuery DRIVER2:0561
   AH = 9: AL = AL - the tune playing. */
void Driver2_TuneQuery(void)
{
    uint8_t t = o8(0x0078);
    R.cf = AL < t;
    SET_AL(AL - t);
    flags_zs8(AL);
}

/* Driver2_EffectQuery DRIVER2:0566
   AH = 0Ah: AL = 0 if a voice plays effect AL, else AL unchanged. */
void Driver2_EffectQuery(void)
{
    R.di = 0x009f;
    R.cx = 4;
    SET_AH(AL);
    do {
        uint8_t e = o8((uint16_t)(R.di + 0x0e));
        R.cf = AL < e;
        SET_AL(AL - e);
        flags_zs8(AL);
        if (AL == 0) return;
        SET_AL(AH);
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
}

/* Driver2_CountEffects DRIVER2:057b
   AH = 0Bh: AX = 0200h + the number of effect voices. */
void Driver2_CountEffects(void)
{
    R.di = 0x009f;
    R.cx = 4;
    R.ax = 0x0200;
    do {
        if (AH == o8((uint16_t)(R.di + 0x0a))) SET_AL(AL + 1);
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
}

/* Driver2_Info0C DRIVER2:0591 / Driver2_Info0D DRIVER2:0595
   AH = 0Ch: AX = 57h; AH = 0Dh: AX = 67h (constants; the game never asks). */
void Driver2_Info0C(void) { R.ax = 0x57; }
void Driver2_Info0D(void) { R.ax = 0x67; }

/* Driver2_EngineOn DRIVER2:0647
   AH = 0Eh: engine AL (0, else 1) at PIT count CX, on. AL = 0. */
void Driver2_EngineOn(void)
{
    if (AL != 0) {
        o16w(0x009d, R.cx);
        o8w(0x009a, o8(0x009a) | 2);
    } else {
        o16w(0x009b, R.cx);
        o8w(0x009a, o8(0x009a) | 1);
    }
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_EngineTone DRIVER2:0663
   AH = 0Fh: engine AL's count := CX, on or off as it was. AL = 0. */
void Driver2_EngineTone(void)
{
    o16w(AL != 0 ? 0x009d : 0x009b, R.cx);
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_EngineOff DRIVER2:0675
   AH = 10h: engine AL off. AL = 0. */
void Driver2_EngineOff(void)
{
    o8w(0x009a, o8(0x009a) & (AL != 0 ? 0xfd : 0xfe));
    SET_AL(0);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_Tick DRIVER2:0185
   AH = 3, once per frame: unless one is running (AX = FFFEh), the requests (stop all, stop the music,
   the tune, the effects to start, then those to stop), the clocks, the four voices, and the speaker's
   note for this tick. */
void Driver2_Tick(void)
{
    if (o8(0x007d) != 0) {
        R.ax = 0xfffe;
        R.cf = false, R.zf = false, R.sf = false;
        return;
    }
    o8w(0x007d, (uint8_t)(o8(0x007d) + 1));
    Driver2_DoStopAll();
    Driver2_DoStopMusic();
    Driver2_StartTune();
    Driver2_StartEffects();
    Driver2_StopEffects();
    for (uint16_t si = 0x007e; si <= 0x0086; si = (uint16_t)(si + 8)) {
        R.si = si;
        R.ax = o16(si);
        uint32_t s = (uint32_t)o16((uint16_t)(si + 4)) + R.ax;
        o16w((uint16_t)(si + 4), (uint16_t)s);
        R.ax = o16((uint16_t)(si + 2));
        o16w((uint16_t)(si + 6), (uint16_t)(o16((uint16_t)(si + 6)) + R.ax + (s >> 16)));
    }
    R.di = 0x009f;
    R.cx = 4;
    do {
        uint16_t cx = R.cx;
        Driver2_VoiceStep();
        R.cx = cx;
        R.di = (uint16_t)(R.di + 0x12);
    } while (--R.cx);
    o16w(0x0084, R.cx);
    o16w(0x008c, R.cx);
    Driver2_SpeakerOut();
    SET_AL(0);
    o8w(0x007d, AL);
    R.cf = false, R.zf = true, R.sf = false;
}

/* Driver2_Dispatch DRIVER2:00f7 (from Driver2_Entry DRIVER2:0000; 0004 the same as an interrupt
   handler)
   The function AH through the table at 0125 (17 routines, 0..10h) if AH <= [0124], else AX = FFFFh;
   BX, CX, DX, SI, DI, DS and the flags kept. */
void Driver2_Dispatch(void)
{
    Cpu saved = R;
    R.ds = SEG_OVL;
    R.df = false;
    if (AH > o8(0x0124)) {
        R = saved;
        R.ax = 0xffff;
        return;
    }
    uint16_t target = o16((uint16_t)(0x0125 + (uint8_t)(AH << 1)));
    switch (target) {
    case 0x0147: Driver2_Init(); break;
    case 0x016e: Driver2_Shutdown(); break;
    case 0x017a: Driver2_SetRate(); break;
    case 0x0185: Driver2_Tick(); break;
    case 0x0392: Driver2_PlayTune(); break;
    case 0x0431: Driver2_PlayEffect(); break;
    case 0x04b3: Driver2_StopAll(); break;
    case 0x04e8: Driver2_StopMusic(); break;
    case 0x042c: Driver2_StopEffect(); break;
    case 0x0561: Driver2_TuneQuery(); break;
    case 0x0566: Driver2_EffectQuery(); break;
    case 0x057b: Driver2_CountEffects(); break;
    case 0x0591: Driver2_Info0C(); break;
    case 0x0595: Driver2_Info0D(); break;
    case 0x0647: Driver2_EngineOn(); break;
    case 0x0663: Driver2_EngineTone(); break;
    case 0x0675: Driver2_EngineOff(); break;
    default: {
        char m[96];
        snprintf(m, sizeof m, "DRIVER2: function %02xh jumps to %04x; not run", AH, target);
        plat_log(m);
    }
    }
    uint16_t ax = R.ax, bp = R.bp, es = R.es;
    R = saved;
    R.ax = ax, R.bp = bp, R.es = es;
}

/* Driver2_Entry DRIVER2:0000 */
void Driver2_Entry(void) { Driver2_Dispatch(); }
