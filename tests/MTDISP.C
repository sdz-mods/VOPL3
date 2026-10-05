/* MTDISP.EXE - write a message to an MT-32's front-panel display through
 * VOPL3's MPU-401 port, and say exactly what was pushed at the port.
 *
 * Why this exists: the MIDI bridge forwards note messages (midiOutShortMsg)
 * and SysEx (midiOutLongMsg) by two completely different calls, and the
 * second one was found to be failing silently while music played perfectly.
 * A game cannot tell those apart. This can: one SysEx, nothing else, and the
 * display is the receipt.
 *
 * Reading the result is a ladder, from the port outwards:
 *   nothing in the control panel's sysex= counter
 *                       -> the bytes never reached the renderer: the VxD
 *                          trap or the MIDI parser lost them
 *   sysex=1/30b         -> the renderer assembled it AND the MIDI driver took
 *                          it. If the display is still unchanged after that,
 *                          what is downstream is not an MT-32 (or not wired
 *                          to listen)
 *   << REFUSED BY DRIVER -> winmm would not send it; the error code names
 *                          which call said no
 *
 * The MT-32 message itself (Roland DT1 to the display area):
 *   F0 41 10 16 12  20 00 00  <20 ASCII bytes>  <checksum>  F7
 * where checksum = (0x80 - (sum of address and data bytes & 0x7F)) & 0x7F.
 * A stock MT-32 shows the text until something else writes the display.
 *
 * Build (Open Watcom, 16-bit real-mode DOS): see build-mtdisp.ps1
 *
 * usage: MTDISP [mode] [text] [repeat]
 *   mode   U  UART mode (0x3F), raw bytes at the data port      [default]
 *          I  intelligent mode, 0xDF "host sends a system message"
 *          D  intelligent mode, 0xD0 "send data to track 0" - the way
 *             Sierra's SCI drivers do it, which is the path a game takes
 *   text   up to 20 characters; quote it if it has spaces
 *   repeat how many times to send it, each with a counter appended, so a
 *          burst can be watched for pacing (default 1)
 */
#include <conio.h>
#include <dos.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPU_DATA   0x330
#define MPU_CMD    0x331
#define ST_NODATA  0x80            /* 0x331 bit 7: nothing to read */
#define ST_NOWRITE 0x40            /* 0x331 bit 6: not ready for a write */
#define MSG_ACK    0xFE

#define DISP_LEN   20              /* the MT-32's display is 20 characters */

static FILE *log_f;

static void say(const char *fmt, ...)
{
    /* Both at once: the screen for watching, the file for copying off the
     * machine - a DOS box can be closed before anything is read. */
    char    buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    if (log_f) { fputs(buf, log_f); fflush(log_f); }
}

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

static int mpu_get(void)
{
    long spin = 200000L;
    while ((inp(MPU_CMD) & ST_NODATA) && --spin) { }
    if (inp(MPU_CMD) & ST_NODATA) return -1;
    return inp(MPU_DATA) & 0xFF;
}

/* --- the message -------------------------------------------------------- */
/* Build the display SysEx for `text`, padded to 20 characters. */
static int build(unsigned char *sx, const char *text)
{
    int n = 0, i, sum = 0;

    sx[n++] = 0xF0;
    sx[n++] = 0x41;                /* Roland            */
    sx[n++] = 0x10;                /* device 17 (unit 1) */
    sx[n++] = 0x16;                /* MT-32             */
    sx[n++] = 0x12;                /* DT1 - send data   */
    sx[n++] = 0x20; sx[n++] = 0x00; sx[n++] = 0x00;     /* display address */
    sum = 0x20 + 0x00 + 0x00;
    for (i = 0; i < DISP_LEN; i++) {
        unsigned char c = (unsigned char)(text[i] ? text[i] : ' ');
        if (c < 0x20 || c > 0x7E) c = ' ';   /* the display takes ASCII only */
        sx[n++] = c;
        sum += c;
    }
    sx[n++] = (unsigned char)((0x80 - (sum & 0x7F)) & 0x7F);
    sx[n++] = 0xF7;
    return n;
}

static void dump(const unsigned char *sx, int n)
{
    int i;
    say("  bytes:");
    for (i = 0; i < n; i++) {
        if (i && !(i % 16)) say("\n        ");
        say(" %02X", sx[i]);
    }
    say("\n");
}

/* --- the three ways to get it out of the port --------------------------- */
static int enter_mode(char mode)
{
    int ack;

    mpu_cmd(0xFF);                          /* reset */
    ack = mpu_get();
    if (ack != MSG_ACK) {
        say("reset (0xFF) answered %d - nothing is emulating the MPU at "
            "0x330/0x331.\n", ack);
        say("Is VOPL3.VXD loaded, and is the renderer running?\n");
        return 0;
    }
    say("reset (0xFF) -> ACK\n");

    if (mode == 'U') {
        mpu_cmd(0x3F);                      /* enter UART mode */
        ack = mpu_get();
        say("UART mode (0x3F) -> %s\n",
            ack == MSG_ACK ? "ACK" : "NO ACK");
        if (ack != MSG_ACK) return 0;
    }
    return 1;
}

/* Hand one complete SysEx to the port the way `mode` says. */
static void send_sysex(char mode, const unsigned char *sx, int n)
{
    int i, ack;

    if (mode == 'I') {
        mpu_cmd(0xDF);                      /* host sends a system message */
        ack = mpu_get();
        say("  0xDF -> %s\n", ack == MSG_ACK ? "ACK" : "NO ACK");
    } else if (mode == 'D') {
        mpu_cmd(0xD0);                      /* send data to track 0 */
        ack = mpu_get();
        say("  0xD0 -> %s\n", ack == MSG_ACK ? "ACK" : "NO ACK");
    }
    for (i = 0; i < n; i++)
        mpu_put(sx[i]);
}

int main(int argc, char **argv)
{
    unsigned char sx[64];
    char          text[DISP_LEN + 1];
    char          mode = 'U';
    int           repeat = 1, i, n;

    log_f = fopen("C:\\MTDISP.LOG", "a");

    strcpy(text, "VOPL3 MT-32 TEST");
    if (argc > 1 && argv[1][0] && !argv[1][1]) {
        mode = (char)(argv[1][0] & ~0x20);           /* to upper */
        if (mode != 'U' && mode != 'I' && mode != 'D') {
            printf("mode must be U, I or D - see the source for what each"
                   " does\n");
            return 1;
        }
        if (argc > 2) { strncpy(text, argv[2], DISP_LEN); text[DISP_LEN] = 0; }
        if (argc > 3) repeat = atoi(argv[3]);
    } else if (argc > 1) {
        strncpy(text, argv[1], DISP_LEN); text[DISP_LEN] = 0;
        if (argc > 2) repeat = atoi(argv[2]);
    }
    if (repeat < 1) repeat = 1;

    say("\n--- MTDISP: MT-32 display write through the MPU-401 port ---\n");
    say("mode %c (%s), %d message(s), text \"%s\"\n", mode,
        mode == 'U' ? "UART, raw bytes" :
        mode == 'I' ? "intelligent, 0xDF system message" :
                      "intelligent, 0xD0 send-data (Sierra's way)",
        repeat, text);

    if (!enter_mode(mode)) { if (log_f) fclose(log_f); return 1; }

    for (i = 0; i < repeat; i++) {
        char t[DISP_LEN + 1];

        if (repeat > 1) sprintf(t, "%.16s %d", text, i + 1);
        else            strcpy(t, text);
        n = build(sx, t);
        say("send %d of %d: \"%s\"\n", i + 1, repeat, t);
        if (!i) dump(sx, n);                /* the first one in full */
        send_sysex(mode, sx, n);
    }

    /* Leave the card as it was found, so a game started afterwards sees a
     * reset MPU rather than one still in UART mode. */
    mpu_cmd(0xFF);
    mpu_get();

    say("done - %d x %d bytes pushed at the port.\n", repeat, n);
    say("Now check the MT-32's display, and the control panel:\n");
    say("  sysex=%d/%db        the renderer took it AND the MIDI driver "
        "accepted it\n", repeat, repeat * n);
    say("  no sysex= at all    the bytes never got out of the VxD\n");
    say("  REFUSED BY DRIVER   winmm would not send it (the code says which "
        "call)\n");
    say("A display that stays unchanged with sysex=%d/%db counted means the "
        "message\nleft Windows and whatever is listening is not an MT-32.\n",
        repeat, repeat * n);

    if (log_f) { fputs("\n", log_f); fclose(log_f); }
    return 0;
}
