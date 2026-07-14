/* OPLWIN32.EXE - Win32 test: write OPL3 registers straight to ports 388-38B
 * from an ordinary ring-3 Win32 process.
 *
 * Plays a short 3-voice piece (~14 s): bass + sustained pad chords + an
 * arpeggio melody over a I-V-vi-IV progression, twice (second pass an octave
 * up), then a closing chord. The pad runs on HIGH-BANK channels 9-11, so
 * ports 38A/38B carry real register traffic; voices use different waveforms,
 * stereo panning and vibrato. Everything is Sleep()-paced - no busy-waiting.
 *
 * Build: see build-oplwin32.ps1 (Open Watcom, Win32 console, runs on Win9x).
 * NOTE: direct port I/O from ring 3 is a Win9x-only capability - this will not
 * do anything meaningful on Windows NT/2000/XP+.
 */
#include <windows.h>
#include <conio.h>
#include <stdio.h>

#ifndef FILE_FLAG_DELAYED_ERROR
#define FILE_FLAG_DELAYED_ERROR 0x10000000
#endif

#define IOCTL_VOPL3_STAT 0x1001   /* out: [ring_head, ring_tail, ring_lost] */

static void opl(int reg, int val)      /* low bank  -> ports 388 / 389 */
{
    outp(0x388, reg);
    outp(0x389, val);
}

static void opl_hi(int reg, int val)   /* high bank -> ports 38A / 38B */
{
    outp(0x38A, reg);
    outp(0x38B, val);
}

static void wr(int hi, int reg, int val)
{
    if (hi) opl_hi(reg, val); else opl(reg, val);
}

/* one operator: characteristic / level / attack-decay / sustain-release / wave */
static void setop(int hi, int off, int chr, int lvl, int ad, int sr, int wave)
{
    wr(hi, 0x20 + off, chr);
    wr(hi, 0x40 + off, lvl);
    wr(hi, 0x60 + off, ad);
    wr(hi, 0x80 + off, sr);
    wr(hi, 0xE0 + off, wave);
}

/* channels 0-2 (and 9-11 on the high bank): op offsets 0/3, 1/4, 2/5 */
static void setpatch(int hi, int ch,
                     int chr1, int lvl1, int ad1, int sr1, int wave1,
                     int chr2, int lvl2, int ad2, int sr2, int wave2,
                     int fbcon_pan)
{
    setop(hi, ch,     chr1, lvl1, ad1, sr1, wave1);   /* modulator */
    setop(hi, ch + 3, chr2, lvl2, ad2, sr2, wave2);   /* carrier   */
    wr(hi, 0xC0 + ch, fbcon_pan);
}

static void keyon(int hi, int ch, int f, int b)
{
    wr(hi, 0xA0 + ch, f & 0xFF);
    wr(hi, 0xB0 + ch, 0x20 | ((b & 7) << 2) | (f >> 8));
}

static void keyoff(int hi, int ch, int f, int b)
{
    wr(hi, 0xB0 + ch, ((b & 7) << 2) | (f >> 8));
}

static void show_stat(HANDLE h, const char *when)
{
    DWORD out[3] = { 0, 0, 0 }, ret = 0;
    if (DeviceIoControl(h, IOCTL_VOPL3_STAT, NULL, 0, out, sizeof(out), &ret, NULL))
        printf("  ring %-6s head=%lu tail=%lu lost=%lu\n",
               when, out[0], out[1], out[2]);
    else
        printf("  ring %-6s (STAT ioctl failed)\n", when);
}

/* F-numbers (block-relative): C D E F G A B */
#define NC 345
#define ND 387
#define NE 434
#define NF 460
#define NG 517
#define NA 580
#define NB 651

typedef struct { int f, b; } NOTE;

int main(void)
{
    /* I-V-vi-IV in C major: chord tones (close voicing) + bass root */
    static const NOTE chords[4][3] = {
        { {NC,4}, {NE,4}, {NG,4} },     /* C  */
        { {NG,3}, {NB,3}, {ND,4} },     /* G  */
        { {NA,3}, {NC,4}, {NE,4} },     /* Am */
        { {NF,3}, {NA,3}, {NC,4} },     /* F  */
    };
    static const NOTE bass[4] = { {NC,2}, {NG,2}, {NA,2}, {NF,2} };
    /* arpeggio pattern: indices into the chord (3 = root an octave up) */
    static const int arp[8] = { 0, 1, 2, 1, 3, 2, 1, 2 };

    HANDLE h;
    int pass, bar, step, i;

    h = CreateFile("\\\\.\\VOPL3", 0, 0, NULL, 0, FILE_FLAG_DELAYED_ERROR, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("VOPL3.VXD is not loaded. Aborting: without the trap a ring-3 OUT\n"
               "to 388 would go nowhere (or fault). Install VOPL3 and retry.\n");
        return 1;
    }
    printf("VOPL3 open. Writing OPL3 registers from a Win32 (ring-3) process...\n");
    printf("Playing ~14 s: bass + pad chords (high bank, 38A/38B) + arpeggios.\n");
    show_stat(h, "before");

    opl_hi(0x05, 0x01);                 /* OPL3 mode on                          */
    opl(0x01, 0x20);                    /* waveform-select enable                */
    opl(0xBD, 0xC0);                    /* deep vibrato + tremolo, rhythm off    */

    /* melody: ch0 low bank - bright FM lead, carrier vibrato, both speakers */
    setpatch(0, 0, 0x01, 0x18, 0xF2, 0x24, 0,
                   0x41, 0x00, 0xF2, 0x34, 1, 0x30 | 0x06);
    /* bass: ch2 low bank - round and punchy, both speakers */
    setpatch(0, 2, 0x00, 0x14, 0xF4, 0x36, 0,
                   0x01, 0x00, 0xF4, 0x45, 1, 0x30 | 0x08);
    /* pad: ch9-11 HIGH bank - soft slow-attack chord, spread across stereo */
    setpatch(1, 0, 0x01, 0x28, 0x52, 0x14, 2,
                   0x01, 0x14, 0x52, 0x24, 2, 0x10 | 0x02);  /* left   */
    setpatch(1, 1, 0x01, 0x28, 0x52, 0x14, 2,
                   0x01, 0x14, 0x52, 0x24, 2, 0x30 | 0x02);  /* center */
    setpatch(1, 2, 0x01, 0x28, 0x52, 0x14, 2,
                   0x01, 0x14, 0x52, 0x24, 2, 0x20 | 0x02);  /* right  */

    for (pass = 0; pass < 2; pass++) {          /* 2nd pass: melody 8va up */
        for (bar = 0; bar < 4; bar++) {
            const NOTE *ch = chords[bar];

            for (i = 0; i < 3; i++)             /* pad chord for the bar */
                keyon(1, i, ch[i].f, ch[i].b);
            keyon(0, 2, bass[bar].f, bass[bar].b);

            for (step = 0; step < 8; step++) {  /* 8 melody eighths */
                NOTE n = ch[arp[step] & 3];
                if (arp[step] == 3) n = ch[0], n.b++;     /* root 8va  */
                n.b += pass;                              /* pass 2 up */
                if (step == 4)                  /* re-strike bass mid-bar */
                    keyon(0, 2, bass[bar].f, bass[bar].b);
                keyon(0, 0, n.f, n.b);
                Sleep(130);
                keyoff(0, 0, n.f, n.b);
                Sleep(30);
            }

            for (i = 0; i < 3; i++)
                keyoff(1, i, ch[i].f, ch[i].b);
            keyoff(0, 2, bass[bar].f, bass[bar].b);
        }
    }

    /* closing C chord: pad + bass + melody root, let releases ring out */
    for (i = 0; i < 3; i++) keyon(1, i, chords[0][i].f, chords[0][i].b);
    keyon(0, 2, bass[0].f, bass[0].b);
    keyon(0, 0, NC, 5);
    Sleep(1800);
    for (i = 0; i < 3; i++) keyoff(1, i, chords[0][i].f, chords[0][i].b);
    keyoff(0, 2, bass[0].f, bass[0].b);
    keyoff(0, 0, NC, 5);
    Sleep(800);                                 /* release tails */

    show_stat(h, "after");
    printf("Done. If you heard the piece (and head advanced), Win32 ring-3\n"
           "port writes reach VOPL3 - it is not limited to DOS boxes.\n");
    CloseHandle(h);
    return 0;
}
