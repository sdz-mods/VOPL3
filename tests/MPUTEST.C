/* MPUTEST.EXE - drive VOPL3's MPU-401 INTELLIGENT MODE like a game would.
 *
 * Run in a Windows DOS box with VOPL3.VXD loaded and [midi] intelligent=1 in
 * VOPL3.INI (the renderer passes the IRQ to the driver at startup).
 *
 * What a game's MPU driver does, and therefore what this does:
 *   reset (0xFF) -> ACK                     the card is there
 *   0xAC         -> ACK, version            and it is an INTELLIGENT one
 *                                           (VOPL3's old stub ACKs but has no
 *                                           version to give, so this is the
 *                                           test for which half answered)
 *   hook the IRQ, unmask the PIC
 *   0xE0 + tempo, timebase, 0xE7 + rate     set the clock up
 *   0x95                                    clock-to-host interrupts on
 *   0xEC + mask, then 0x08                  arm track 0 and start playing
 *   service the IRQ: read 0x330, answer
 *     0xF0+n (data request) with a timing
 *     byte and a MIDI event; count 0xFD
 *     (clock) messages
 *
 * Reported: whether the engine answered at all, how many clock messages and
 * data requests arrived, and the measured interval between clock messages
 * (which is what says the card's clock keeps real time).
 *
 * Build (Open Watcom, 16-bit real-mode DOS): see build-mputest.ps1
 * usage: MPUTEST [irq] [seconds]          (default: 9, 3)
 */
#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>

#define MPU_DATA   0x330
#define MPU_CMD    0x331
#define ST_NODATA  0x80            /* 0x331 bit 7: nothing to read */
#define ST_NOWRITE 0x40            /* 0x331 bit 6: not ready for a write */

#define MSG_ACK    0xFE
#define MSG_CLOCK  0xFD
#define MSG_END    0xFC
#define MSG_CMDREQ 0xF9

static volatile unsigned long n_clock, n_req, n_end, n_other, n_ints, n_cond;
static volatile unsigned char last_byte;
static unsigned irq_used;
static void (__interrupt __far *old_vec)();

/* --- port helpers ------------------------------------------------------- */
static void mpu_cmd(unsigned char c)
{
    int spin = 10000;
    while ((inp(MPU_CMD) & ST_NOWRITE) && --spin) { }
    outp(MPU_CMD, c);
}

static void mpu_put(unsigned char c)
{
    int spin = 10000;
    while ((inp(MPU_CMD) & ST_NOWRITE) && --spin) { }
    outp(MPU_DATA, c);
}

/* read one byte, waiting up to ~roughly a few ms; -1 = nothing came */
static int mpu_get(void)
{
    long spin = 200000L;
    while ((inp(MPU_CMD) & ST_NODATA) && --spin) { }
    if (inp(MPU_CMD) & ST_NODATA) return -1;
    return inp(MPU_DATA) & 0xFF;
}

/* --- the interrupt service routine -------------------------------------- *
 * Exactly what a game does: drain the data port while the card says it has
 * something, answer data requests, count clock messages. */
static void __interrupt __far isr(void)
{
    int b, guard = 32;

    n_ints++;
    while (!(inp(MPU_CMD) & ST_NODATA) && --guard) {
        b = inp(MPU_DATA) & 0xFF;
        last_byte = (unsigned char)b;
        if (b == MSG_CLOCK) {
            n_clock++;
        } else if (b == MSG_END) {
            n_end++;
        } else if (b >= 0xF0 && b <= 0xF7) {      /* send me more for track n */
            n_req++;
            /* A timing byte, then one note event. CHANNEL 2 (0x91), not
             * channel 1: a stock MT-32 assigns Part 1 to channel 2 and
             * ignores channel 1 entirely, so a note sent there is silent on
             * perfectly working hardware. Alternate on and off so it sounds
             * like a pulse instead of piling notes up. */
            outp(MPU_DATA, 24);                   /* 24 ticks from now */
            outp(MPU_DATA, 0x91);
            outp(MPU_DATA, 60);
            outp(MPU_DATA, (n_req & 1) ? 100 : 0);  /* velocity 0 = note off */
        } else if (b == MSG_CMDREQ) {
            /* The conductor wants data: a timing byte, then one command
             * byte. Both have to go back, or the card is left mid-event and
             * the next track reply gets parsed as the missing byte. */
            n_cond++;
            outp(MPU_DATA, 24);
            outp(MPU_DATA, 0xB1);                 /* harmless: tempo back to x1 */
        } else {
            n_other++;
        }
    }

    if (irq_used >= 8) outp(0xA0, 0x20);
    outp(0x20, 0x20);
}

static unsigned vec_of(unsigned irq)
{
    return (irq < 8) ? (0x08 + irq) : (0x70 + (irq - 8));
}

static unsigned long bios_ticks(void)
{
    unsigned long far *t = (unsigned long far *)MK_FP(0x0040, 0x006C);
    return *t;
}

/* Play for `secs` seconds with the interrupt hooked, and leave the counters
 * holding what happened. Used by the two phases below. */
static void play_for(unsigned irq, unsigned secs)
{
    unsigned long t0 = bios_ticks();
    (void)irq;
    while ((bios_ticks() - t0) < (unsigned long)secs * 18) { }
}

/* Does the conductor track run, and does using it leave the normal tracks
 * alone? The conductor is the card's command-carrying track: with it enabled
 * the card asks for conductor data (0xF9) as well as track data (0xF0+n), and
 * a reply to one must never be taken for the other.
 * Pass: conductor requests arrive AND track requests keep arriving with them. */
static void test_conductor(unsigned irq, unsigned secs, FILE *log)
{
    n_req = n_cond = 0;
    mpu_cmd(0xFF); (void)mpu_get();           /* reset */
    mpu_cmd(0xC4); (void)mpu_get();           /* timebase 96 */
    mpu_cmd(0xE0); mpu_put(100); (void)mpu_get();
    mpu_cmd(0x8F); (void)mpu_get();           /* conductor ON */
    mpu_cmd(0xEC); mpu_put(0x01); (void)mpu_get();
    mpu_cmd(0x08); (void)mpu_get();           /* play */
    play_for(irq, secs);
    mpu_cmd(0x04);                            /* stop */

    printf("  conductor:   %lu conductor requests, %lu track requests  -> %s\n",
           n_cond, n_req,
           (n_cond && n_req) ? "both running" :
           (n_cond ? "TRACKS STALLED" : "conductor silent"));
    if (log) fprintf(log, "conductor: cond=%lu track=%lu\n", n_cond, n_req);
}

/* A command in the 0xE0-0xEF range that takes NO parameter, sent while tracks
 * are playing. If the card wrongly waits for one it swallows the next timing
 * byte, the event misparses and track requests dry up.
 * Pass: requests keep coming at roughly the rate they did before. */
static void test_stray_command(unsigned irq, unsigned secs, FILE *log)
{
    unsigned long before, after;

    n_req = n_cond = 0;
    mpu_cmd(0xFF); (void)mpu_get();
    mpu_cmd(0xC4); (void)mpu_get();
    mpu_cmd(0xE0); mpu_put(100); (void)mpu_get();
    mpu_cmd(0xEC); mpu_put(0x01); (void)mpu_get();
    mpu_cmd(0x08); (void)mpu_get();
    play_for(irq, secs);
    before = n_req;

    mpu_cmd(0xE8);                            /* no parameter follows this */
    play_for(irq, secs);
    after = n_req - before;
    mpu_cmd(0x04);

    printf("  stray 0xE8:  %lu requests before, %lu after  -> %s\n",
           before, after,
           (after && after * 2 >= before) ? "unaffected" : "STREAM BROKEN");
    if (log) fprintf(log, "stray cmd: before=%lu after=%lu\n", before, after);
}

int main(int argc, char **argv)
{
    FILE *log;
    unsigned irq  = (argc > 1) ? (unsigned)atoi(argv[1]) : 9;
    unsigned secs = (argc > 2) ? (unsigned)atoi(argv[2]) : 3;
    unsigned char m21, ma1;
    unsigned long t0;
    int ack, ver, rev;
    double per;

    if (irq < 3 || irq > 15) {
        /* Say so rather than quietly testing something else. IRQ 2 is the
         * cascade: VPICD will not hand it out, and the BIOS INT 71h -> 0Ah
         * redirection that makes "IRQ 2" work on real hardware does not
         * happen in a DOS box. */
        printf("\n  IRQ %u cannot be used%s - testing IRQ 9 instead.\n",
               irq, (irq == 2) ? " (it is the cascade)" : "");
        irq = 9;
    }
    if (secs < 1 || secs > 20) secs = 3;
    irq_used = irq;

    printf("\nMPUTEST - MPU-401 intelligent mode, IRQ %u, %u s\n\n", irq, secs);
    log = fopen("C:\\MPUTEST.LOG", "w");
    if (log) fprintf(log, "MPUTEST irq=%u secs=%u\n", irq, secs);

    /* --- is anything there, and is it the intelligent engine? --- */
    mpu_cmd(0xFF);                      /* reset */
    ack = mpu_get();
    mpu_cmd(0xAC);                      /* version */
    ver = mpu_get();                    /* ACK ... */
    if (ver == MSG_ACK) ver = mpu_get();/* ... then the version byte */
    mpu_cmd(0xAD);                      /* revision */
    rev = mpu_get();
    if (rev == MSG_ACK) rev = mpu_get();

    printf("  reset -> %02Xh,  version -> %02Xh,  revision -> %02Xh\n",
           ack & 0xFF, ver & 0xFF, rev & 0xFF);
    if (log) fprintf(log, "reset=%02X version=%02X revision=%02X\n",
                     ack & 0xFF, ver & 0xFF, rev & 0xFF);
    if (ack != MSG_ACK) {
        printf("\n  No ACK: is VOPL3 loaded, with MIDI = VOPL3 at install?\n");
        if (log) { fprintf(log, "no ack - giving up\n"); fclose(log); }
        return 1;
    }
    if (ver != 0x15) {
        printf("\n  No version byte: intelligent mode is OFF (the old stub is\n"
               "  answering). Set [midi] intelligent=1 in VOPL3.INI, restart\n"
               "  the renderer and reboot.\n");
        if (log) { fprintf(log, "stub answered - intelligent mode off\n"); fclose(log); }
        return 1;
    }

    /* --- hook the interrupt --- */
    m21 = (unsigned char)inp(0x21);
    ma1 = (unsigned char)inp(0xA1);
    old_vec = _dos_getvect(vec_of(irq));
    _dos_setvect(vec_of(irq), isr);
    if (irq >= 8) {
        outp(0xA1, inp(0xA1) & ~(1 << (irq - 8)));
        outp(0x21, inp(0x21) & ~(1 << 2));
    } else {
        outp(0x21, inp(0x21) & ~(1 << irq));
    }
    _enable();

    /* --- set the clock up and start playing --- */
    mpu_cmd(0xC4); (void)mpu_get();     /* timebase 96 */
    mpu_cmd(0xE0); mpu_put(100);        /* tempo 100 */
    (void)mpu_get();
    mpu_cmd(0xE7); mpu_put(10);         /* clock-to-host every 10 ticks */
    (void)mpu_get();
    mpu_cmd(0x95); (void)mpu_get();     /* clock-to-host ON */
    mpu_cmd(0xEC); mpu_put(0x01);       /* track 0 active */
    (void)mpu_get();
    mpu_cmd(0x08); (void)mpu_get();     /* start playing */

    t0 = bios_ticks();
    while ((bios_ticks() - t0) < (unsigned long)secs * 18) { }

    mpu_cmd(0x04);                      /* stop playing */

    printf("\n");
    test_conductor(irq, 2, log);
    test_stray_command(irq, 2, log);

    /* --- unhook --- */
    outp(0x21, m21);
    outp(0xA1, ma1);
    _dos_setvect(vec_of(irq), old_vec);

    /* 100 BPM at timebase 96 = 160 ticks/s, one clock message per 10 ticks
     * = 16 per second. */
    per = n_clock ? (double)secs * 1000.0 / (double)n_clock : 0;
    printf("\n  interrupts %lu,  clock msgs %lu (%.1f ms apart, want 62.5),\n"
           "  data requests %lu,  end %lu,  other %lu\n",
           n_ints, n_clock, per, n_req, n_end, n_other);
    if (log) {
        fprintf(log, "ints=%lu clock=%lu per=%.1fms req=%lu end=%lu other=%lu\n",
                n_ints, n_clock, per, n_req, n_end, n_other);
        fclose(log);
    }
    printf("\n  Done. Also written to C:\\MPUTEST.LOG.\n");
    return 0;
}
