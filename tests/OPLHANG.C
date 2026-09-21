/* OPLHANG.C - leaves OPL3 notes hanging on exit, to test what VOPL3 does
 * when a game quits without silencing the chip (e.g. Commander Keen 4).
 *
 * It keys on a sustaining chord - two voices on bank 0 (ports 0x388/0x389)
 * and one on bank 1 (0x38A/0x38B, OPL3 mode) - and exits WITHOUT any
 * key-off. With a sustaining patch (EG-TYP set) the chord holds at its
 * sustain level for as long as KEY-ON stays set.
 *
 *   OPLHANG        key on the chord and exit, leaving it hanging
 *   OPLHANG /OFF   key off every voice on both banks (cleanup after a test
 *                  on a VOPL3 build that leaves the notes playing)
 *
 * The notes belong to the DOS box, so what matters is what happens when the
 * DOS BOX CLOSES (type EXIT), not when the program returns to the prompt.
 *
 * Build (Open Watcom, 16-bit real-mode DOS): see build-oplhang.ps1
 */
#include <conio.h>
#include <stdio.h>
#include <string.h>

/* short I/O delay: OPL wants a settle time between address and data writes.
 * Reading the status port a few times is the classic AdLib delay. */
static void iowait(int n) { while (n-- > 0) (void)inp(0x388); }

/* bank 0 = 0x388/0x389, bank 1 = 0x38A/0x38B */
static void opl(int bank, unsigned char reg, unsigned char val)
{
    unsigned port = bank ? 0x38A : 0x388;
    outp(port, reg);     iowait(6);
    outp(port + 1, val); iowait(35);
}

/* operator register offset of each channel's modulator slot */
static const unsigned char op_off[9] = {0,1,2,8,9,10,16,17,18};

/* sustaining "organ" patch with a release of about a second, so a key-off
 * is heard as a short fade rather than a cut */
static void setup_channel(int bank, int ch)
{
    unsigned char m = op_off[ch];       /* modulator operator */
    unsigned char c = op_off[ch] + 3;   /* carrier operator   */
    opl(bank, 0x20 + m, 0x21);   /* EG-TYP=1 (sustaining), mult=1   */
    opl(bank, 0x20 + c, 0x21);
    opl(bank, 0x40 + m, 0x10);   /* modulator output level          */
    opl(bank, 0x40 + c, 0x04);   /* carrier near full volume        */
    opl(bank, 0x60 + m, 0xF2);   /* attack fast, decay medium       */
    opl(bank, 0x60 + c, 0xF2);
    opl(bank, 0x80 + m, 0x35);   /* sustain high, release rate 5    */
    opl(bank, 0x80 + c, 0x35);
    opl(bank, 0xC0 + ch, 0x31);  /* L+R out, feedback, FM (OPL3)    */
    opl(bank, 0xE0 + m, 0x00);   /* sine waveform                   */
    opl(bank, 0xE0 + c, 0x00);
}

/* F-numbers for one octave */
static const unsigned short fnum[12] = {
    0x157,0x16B,0x181,0x198,0x1B0,0x1CA,0x1E5,0x202,0x220,0x241,0x263,0x287
};

static void note_on(int bank, int ch, int semitone, int block)
{
    unsigned short f = fnum[semitone];
    opl(bank, 0xA0 + ch, (unsigned char)(f & 0xFF));
    opl(bank, 0xB0 + ch, (unsigned char)(0x20 | (block << 2) | ((f >> 8) & 0x03)));
}

int main(int argc, char **argv)
{
    int bank, ch;

    if (argc > 1 && (!stricmp(argv[1], "/OFF") || !stricmp(argv[1], "-OFF"))) {
        for (bank = 0; bank < 2; bank++)
            for (ch = 0; ch < 9; ch++)
                opl(bank, 0xB0 + ch, 0x00);
        opl(0, 0xBD, 0x00);           /* rhythm key bits too */
        printf("OPLHANG: all voices keyed off.\n");
        return 0;
    }

    opl(0, 0x01, 0x20);               /* enable waveform select          */
    opl(1, 0x05, 0x01);               /* OPL3 mode, so bank 1 is audible */

    setup_channel(0, 0);
    setup_channel(0, 1);
    setup_channel(1, 0);
    note_on(0, 0, 4, 3);              /* bank 0 ch0: E  */
    note_on(0, 1, 11, 3);             /* bank 0 ch1: B  */
    note_on(1, 0, 4, 4);              /* bank 1 ch0: E, an octave up */

    printf("OPLHANG: chord keyed on (bank 0 ch0+ch1, bank 1 ch0), exiting\n");
    printf("WITHOUT a key-off - the chord should keep playing now.\n\n");
    return 0;
}
