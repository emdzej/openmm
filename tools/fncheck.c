/* Checks one ported routine against a recording of the original (tools/emu/capture.py): the memory and
   registers at the routine's entry are loaded, the port is run with its ports, ints and driver calls
   served from the recording in order (each checked: the same port, the same value written, the same
   registers handed to the int), and the memory and registers at its return are compared with the
   original's. The stack below the entry SP is not compared (the port keeps pushes in locals).
     build/mm_fncheck out/fn/1000_2efd_1 [more captures...]      exit 0 = all match
   Registers compared: ax bx cx dx si di bp ds es, CF; ZF and SF reported, not failed (-z: failed too). */
#include "cpu.h"
#include "fntab.h"
#include "machine.h"
#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum { NREG = 14 };
typedef struct { uint16_t v[NREG]; } RegLine;   /* ax bx cx dx si di bp sp ds es ss flags cs ip */
enum { AX_, BX_, CX_, DX_, SI_, DI_, BP_, SP_, DS_, ES_, SS_, FL_, CS_, IP_ };

typedef struct {
    char kind;        /* I O N D */
    uint16_t port, val;
    uint8_t intno;
    RegLine pre, post;
    int mfirst, mcount;
} Event;

typedef struct { uint32_t lin; uint8_t *data; size_t len; } Span;

static Event *ev;
static int nev, cur;
static Span *spans;
static int nspans;
static int errors;
static const char *who;

static void err(const char *fmt, const char *a, unsigned x, unsigned y)
{
    if (errors++ < 12) {
        printf("  %s: ", who);
        printf(fmt, a, x, y);
        printf("\n");
    }
}

static bool parse_regs(const char *s, RegLine *r)
{
    unsigned v[NREG];
    if (sscanf(s, "%x %x %x %x %x %x %x %x %x %x %x %x %x %x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
               &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13]) != NREG)
        return false;
    for (int i = 0; i < NREG; i++) r->v[i] = (uint16_t)v[i];
    return true;
}

static void cpu_from(const RegLine *r)
{
    R.ax = r->v[AX_], R.bx = r->v[BX_], R.cx = r->v[CX_], R.dx = r->v[DX_];
    R.si = r->v[SI_], R.di = r->v[DI_], R.bp = r->v[BP_], R.sp = r->v[SP_];
    R.ds = r->v[DS_], R.es = r->v[ES_], R.ss = r->v[SS_];
    uint16_t f = r->v[FL_];
    R.cf = f & 1, R.zf = (f >> 6) & 1, R.sf = (f >> 7) & 1, R.df = (f >> 10) & 1;
}

static const char *const regname[] = {"ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ds", "es", "ss"};

/* R against a recorded line: the general registers and the segments, CF (ZF/SF when strict). */
static int strict_flags;
static void cmp_regs(const RegLine *r, const char *when)
{
    uint16_t got[] = {R.ax, R.bx, R.cx, R.dx, R.si, R.di, R.bp, R.sp, R.ds, R.es, R.ss};
    for (int i = 0; i < 11; i++) {
        if (i == SP_ || i == SS_) continue;
        if (got[i] != r->v[i]) {
            char m[64];
            snprintf(m, sizeof m, "%s %s", when, regname[i]);
            err("%s: port %04x, original %04x", m, got[i], r->v[i]);
        }
    }
    uint16_t f = r->v[FL_];
    if (R.cf != (f & 1)) err("%s CF: port %u, original %u", when, R.cf, f & 1);
    if (R.zf != ((f >> 6) & 1)) {
        if (strict_flags) err("%s ZF: port %u, original %u", when, R.zf, (f >> 6) & 1);
        else printf("  %s: note: %s ZF port %u, original %u\n", who, when, R.zf, (f >> 6) & 1);
    }
}

static Event *next(char kind, const char *what)
{
    if (cur >= nev) {
        err("%s: the original did no more (port wants %c)", what, (unsigned)kind, 0);
        return NULL;
    }
    Event *e = &ev[cur++];
    if (e->kind != kind) {
        char m[64];
        snprintf(m, sizeof m, "%s, event %d", what, cur);
        err("%s: port does %c, original %c", m, (unsigned)kind, (unsigned)e->kind);
        return NULL;
    }
    return e;
}

static uint8_t r_in8(uint16_t port)
{
    Event *e = next('I', "in");
    if (!e) return 0xff;
    if (e->port != port) err("%s: port reads %x, original %x", "in", port, e->port);
    return (uint8_t)e->val;
}

static void r_out8(uint16_t port, uint8_t v)
{
    Event *e = next('O', "out");
    if (!e) return;
    if (e->port != port || e->val != v) {
        char m[64];
        snprintf(m, sizeof m, "out %x,%x", port, v);
        err("%s: original out %x,%x", m, e->port, e->val);
    }
}

static void apply(Event *e, const char *what)
{
    cmp_regs(&e->pre, what);
    for (int i = 0; i < e->mcount; i++) {
        Span *s = &spans[e->mfirst + i];
        memcpy(g_mem + s->lin, s->data, s->len);
    }
    uint16_t sp = R.sp, ss = R.ss;
    cpu_from(&e->post);
    R.sp = sp, R.ss = ss;
}

static void r_int(uint8_t n)
{
    Event *e = next('N', "int");
    if (!e) return;
    if (e->intno != n) err("%s: port int %x, original int %x", "int", n, e->intno);
    char m[32];
    snprintf(m, sizeof m, "int %02xh", n);
    apply(e, m);
}

static void r_drv(void)
{
    Event *e = next('D', "driver call");
    if (e) apply(e, "driver call");
}

static const MachHooks replay = {r_in8, r_out8, r_int, r_drv};

static uint8_t *load(const char *dir, const char *name)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    uint8_t *b = malloc(MEM_SIZE);
    size_t n = fread(b, 1, MEM_SIZE, f);
    fclose(f);
    if (n != MEM_SIZE) { free(b); return NULL; }
    return b;
}

static int hexval(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

static int check(const char *dir)
{
    char p[512], line[1 << 16];
    snprintf(p, sizeof p, "%s/events.txt", dir);
    FILE *f = fopen(p, "r");
    uint8_t *before = load(dir, "before.bin"), *after = load(dir, "after.bin");
    if (!f || !before || !after) {
        printf("%s: not a capture (events.txt, before.bin, after.bin)\n", dir);
        return 2;
    }
    const char *base = strrchr(dir, '/');
    base = base ? base + 1 : dir;
    unsigned seg, off;
    if (sscanf(base, "%4x_%4x", &seg, &off) != 2) {
        printf("%s: the directory is not named seg_off_hit\n", dir);
        return 2;
    }
    const FnEntry *fe = fntab_find(seg << 16 | off);
    who = base;
    if (!fe) { printf("%s: no routine at %04x:%04x in names.tsv\n", base, seg, off); return 2; }
    if (!fe->ported) { printf("%s: %s is not ported yet\n", base, fe->name); return 2; }

    RegLine b = {{0}}, fin = {{0}};
    nev = cur = nspans = 0;
    int cap = 0, scap = 0;
    Event *open = NULL;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        char k = line[0];
        const char *rest = line + 2;
        if (k == 'B') parse_regs(rest, &b);
        else if (k == 'F') parse_regs(rest, &fin);
        else if (k == 'I' || k == 'O' || k == 'N' || k == 'D') {
            if (nev == cap) ev = realloc(ev, (size_t)(cap = cap ? cap * 2 : 256) * sizeof *ev);
            Event *e = &ev[nev++];
            memset(e, 0, sizeof *e);
            e->kind = k;
            unsigned a1, a2;
            if (k == 'I' || k == 'O') {
                sscanf(rest, "%x %x", &a1, &a2);
                e->port = (uint16_t)a1, e->val = (uint16_t)a2;
            } else {
                if (k == 'N') {
                    sscanf(rest, "%x", &a1);
                    e->intno = (uint8_t)a1;
                    rest = strchr(rest, ' ') + 1;
                }
                parse_regs(rest, &e->pre);
                e->mfirst = nspans;
                open = e;
            }
        } else if (k == 'A' && open) {
            parse_regs(rest, &open->post);
        } else if (k == 'M' && open) {
            unsigned lin;
            sscanf(rest, "%x", &lin);
            const char *hex = strchr(rest, ' ') + 1;
            size_t len = strlen(hex) / 2;
            if (nspans == scap) spans = realloc(spans, (size_t)(scap = scap ? scap * 2 : 64) * sizeof *spans);
            Span *s = &spans[nspans++];
            s->lin = lin, s->len = len, s->data = malloc(len);
            for (size_t i = 0; i < len; i++) s->data[i] = (uint8_t)(hexval(hex[2 * i]) << 4 | hexval(hex[2 * i + 1]));
            open->mcount++;
        } else if (k == 'E') {
            open = NULL;
        }
    }
    fclose(f);

    memcpy(g_mem, before, MEM_SIZE);
    cpu_from(&b);
    errors = 0;
    mach_set_hooks(&replay);
    fe->fn();
    mach_set_hooks(NULL);
    if (cur < nev) {
        Event *e = &ev[cur];
        err("the original did %d more events (next: %c %x)", "", (unsigned)(nev - cur), e->kind == 'N' ? e->intno : e->port);
    }
    cmp_regs(&fin, "return");
    /* memory, except the stack below the entry SP */
    uint32_t stack_hi = (uint32_t)b.v[SS_] * 16 + b.v[SP_], stack_lo = stack_hi > 0x1000 ? stack_hi - 0x1000 : 0;
    int ndiff = 0;
    for (uint32_t i = 0; i < MEM_SIZE; i++) {
        if (g_mem[i] == after[i] || (i >= stack_lo && i < stack_hi)) continue;
        if (ndiff++ < 8) {
            char m[64];
            if (i >= 0x193c0 && i < 0x293c0) snprintf(m, sizeof m, "memory %05x (ds:%04x)", i, i - 0x193c0);
            else if (i >= 0x10000 && i < 0x20000) snprintf(m, sizeof m, "memory %05x (cs:%04x)", i, i - 0x10000);
            else snprintf(m, sizeof m, "memory %05x", i);
            err("%s: port %02x, original %02x", m, g_mem[i], after[i]);
        }
    }
    if (ndiff > 8) printf("  %s: ... %d bytes differ\n", who, ndiff);
    if (ndiff) errors++;
    for (int i = 0; i < nspans; i++) free(spans[i].data);
    free(before), free(after);
    printf("%s %s (%s, %d events)\n", errors ? "FAIL" : "ok  ", base, fe->name, nev);
    return errors ? 1 : 0;
}

int main(int argc, char **argv)
{
    int bad = 0, n = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-z")) { strict_flags = 1; continue; }
        bad |= check(argv[i]);
        n++;
    }
    if (!n) {
        printf("usage: mm_fncheck [-z] <out/fn/seg_off_hit>...\n");
        return 2;
    }
    return bad;
}
