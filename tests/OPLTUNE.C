/* OPLTUNE.C - minimal DOS OPL3 FM player, built to test the VOPL3 pipeline
 * WITHOUT a CPU-hungry game in the way.
 *
 * It programs a few OPL3 channels and plays a looping arpeggio on ports
 * 0x388/0x389 (the standard AdLib/OPL address+data pair that VOPL3 traps).
 * While waiting between notes it RELEASES its Win9x time-slice
 * (INT 2Fh AX=1680h) instead of busy-looping, so it uses almost no CPU.
 *
 * Build (Open Watcom, 16-bit real-mode DOS):  see build-opltune.ps1
 * Run in a Win98 DOS box with VOPL3 loaded and voplsrv.exe running.
 * Press a key to quit.
 */
#include <dos.h>
#include <conio.h>
#include <stdio.h>

#define OPL_ADDR 0x388
#define OPL_DATA 0x389

/* short I/O delay: OPL wants a settle time between address and data writes.
 * Reading the status port a few times is the classic AdLib delay. */
static void iowait(int n) { while (n-- > 0) (void)inp(OPL_ADDR); }

static void opl(unsigned char reg, unsigned char val)
{
    outp(OPL_ADDR, reg); iowait(6);
    outp(OPL_DATA, val); iowait(35);
}

/* release this VM's time-slice back to Windows (low-CPU wait) */
static void yield_slice(void)
{
    union REGS r;
    r.x.ax = 0x1680;
    int86(0x2F, &r, &r);
}

static unsigned long ticks(void)
{
    unsigned long far *t = (unsigned long far *)MK_FP(0x40, 0x6C);
    return *t;
}

/* wait ~n BIOS ticks (55 ms each), yielding the CPU the whole time */
static void wait_ticks(unsigned n)
{
    unsigned long start = ticks();
    while (ticks() - start < n) yield_slice();
}

/* F-numbers for one octave (block/octave chosen per note below) */
static const unsigned short fnum[12] = {
    0x157,0x16B,0x181,0x198,0x1B0,0x1CA,0x1E5,0x202,0x220,0x241,0x263,0x287
};

/* program one 2-operator channel with a simple sustained "organ" patch.
 * ch = 0..8 ; operator register offset table for the modulator slot. */
static const unsigned char op_off[9] = {0,1,2,8,9,10,16,17,18};

static void setup_channel(int ch)
{
    unsigned char m = op_off[ch];       /* modulator operator */
    unsigned char c = op_off[ch] + 3;   /* carrier operator   */
    opl(0x20 + m, 0x21);   /* sustaining, mult=1               */
    opl(0x20 + c, 0x21);
    opl(0x40 + m, 0x10);   /* modulator output level           */
    opl(0x40 + c, 0x00);   /* carrier at full volume           */
    opl(0x60 + m, 0xF2);   /* attack fast, decay medium        */
    opl(0x60 + c, 0xF2);
    opl(0x80 + m, 0x33);   /* sustain high, release medium     */
    opl(0x80 + c, 0x33);
    opl(0xC0 + ch, 0x31);  /* L+R out, feedback, FM (OPL3)     */
    opl(0xE0 + m, 0x00);   /* sine waveform                    */
    opl(0xE0 + c, 0x00);
}

static void note_on(int ch, int semitone, int block)
{
    unsigned short f = fnum[semitone % 12];
    opl(0xA0 + ch, (unsigned char)(f & 0xFF));
    opl(0xB0 + ch, (unsigned char)(0x20 | (block << 2) | ((f >> 8) & 0x03)));
}

static void note_off(int ch)
{
    opl(0xB0 + ch, 0x00);
}

int main(void)
{
    static const signed char lead[] = {
        4,4,7,4,  4,2,4,4,  0,11,0,4,  4,7,9,7,
        4,4,7,4,  4,2,4,4,  0,11,0,2,  4,-1,-1,-1
    };
    int i, n = (int)sizeof(lead);
    int loops = 0;

    printf("OPLTUNE - OPL3 FM test player (driving riff). Press a key to stop.\n");
    printf("(Make sure VOPL3 is loaded and voplsrv2.exe is running.)\n");

    opl(0x01, 0x20);       /* enable waveform select (OPL2 compat)  */
    /* enable OPL3 mode via the second register set (0x105 = 1) */
    outp(0x38A, 0x05); iowait(6); outp(0x38B, 0x01); iowait(35);

    /* ch0 = bass pulse, ch1 = lead, ch2+ch3 = sustained power chord (E + B) */
    for (i = 0; i < 4; i++) setup_channel(i);
    note_on(2, 4, 2);      /* E, low octave  */
    note_on(3, 11, 2);     /* B, low octave  */

    /* VOPL3_FINITE: run a fixed number of passes then exit.
       Otherwise loop until a key is pressed. */
    while (!kbhit()
#ifdef VOPL3_FINITE
           && loops < 6
#endif
          ) {
        for (i = 0; i < n; i++) {
            note_off(0);
            note_on(0, 4, 2);              /* low-E bass pulse every step */
            if (lead[i] >= 0) {
                note_off(1);
                note_on(1, lead[i], 4);    /* lead melody, mid octave     */
            }
            wait_ticks(1);                 /* ~55 ms, yielding CPU        */
            if (kbhit()) break;
        }
        loops++;
    }
#ifndef VOPL3_FINITE
    (void)getch();
#endif

    /* silence everything on exit */
    for (i = 0; i < 9; i++) note_off(i);
    printf("stopped.\n");
    return 0;
}
