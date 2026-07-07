/* OPLWIN32.EXE - Win32 test: write OPL3 registers straight to ports 388-38B
 * from an ordinary ring-3 Win32 process.
 *
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

static void show_stat(HANDLE h, const char *when)
{
    DWORD out[3] = { 0, 0, 0 }, ret = 0;
    if (DeviceIoControl(h, IOCTL_VOPL3_STAT, NULL, 0, out, sizeof(out), &ret, NULL))
        printf("  ring %-6s head=%lu tail=%lu lost=%lu\n",
               when, out[0], out[1], out[2]);
    else
        printf("  ring %-6s (STAT ioctl failed)\n", when);
}

int main(void)
{
    /* C major scale, F-numbers for block 4 (C4..C5) */
    static const int notes[] = { 345, 387, 434, 460, 517, 580, 651, 690 };
    HANDLE h;
    int i, f;

    h = CreateFile("\\\\.\\VOPL3", 0, 0, NULL, 0, FILE_FLAG_DELAYED_ERROR, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("VOPL3.VXD is not loaded. Aborting: without the trap a ring-3 OUT\n"
               "to 388 would go nowhere (or fault). Install VOPL3 and retry.\n");
        return 1;
    }
    printf("VOPL3 open. Writing OPL3 registers from a Win32 (ring-3) process...\n");
    show_stat(h, "before");

    opl_hi(0x05, 0x01);                 /* OPL3 mode on (also exercises 38A/38B) */
    opl(0x01, 0x20);                    /* waveform-select enable                */
    opl(0x20, 0x01); opl(0x23, 0x01);   /* op mult = 1                           */
    opl(0x40, 0x10); opl(0x43, 0x00);   /* modulator level / carrier full        */
    opl(0x60, 0xF0); opl(0x63, 0xF0);   /* attack/decay                          */
    opl(0x80, 0x77); opl(0x83, 0x77);   /* sustain/release                       */
    opl(0xC0, 0x30);                    /* channel 0: L+R enable, FM connection  */

    for (i = 0; i < 8; i++) {
        f = notes[i];
        opl(0xA0, f & 0xFF);
        opl(0xB0, 0x30 | (f >> 8));     /* key-on,  block 4 */
        Sleep(300);
        opl(0xB0, 0x10 | (f >> 8));     /* key-off, block 4 */
        Sleep(60);
    }

    show_stat(h, "after");
    printf("Done. If you heard the scale (and head advanced), Win32 ring-3\n"
           "port writes reach VOPL3 - it is not limited to DOS boxes.\n");
    CloseHandle(h);
    return 0;
}
