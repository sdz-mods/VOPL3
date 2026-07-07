/* SBTEST.C - probe the SoundBlaster DSP (SBEMUL)
 *
 * Resets the DSP at base 0x220 and reads its version. Reports to the screen
 * AND to COM1 (0x3F8).
 *
 *   SB DSP present -> reset returns 0xAA + a version  => SBEMUL digital alive
 *   SB DSP silent  -> reset times out / wrong byte    => SBEMUL disabled
 *
 * Build (Open Watcom, 16-bit DOS): see build-sbtest.ps1
 */
#include <dos.h>
#include <conio.h>
#include <stdio.h>

#define SB      0x220
#define SB_RESET (SB + 0x6)
#define SB_READ  (SB + 0xA)   /* read data            */
#define SB_WRITE (SB + 0xC)   /* write cmd/data; bit7=busy */
#define SB_RSTAT (SB + 0xE)   /* read-buf status; bit7=data ready */

#define COM1 0x3F8
static int ser_ok = 0;
static void ser_init(void){ outp(COM1+1,0); outp(COM1+3,0x80); outp(COM1,1); outp(COM1+1,0);
                            outp(COM1+3,3); outp(COM1+2,0xC7); ser_ok=1; }
static void ser_ch(char c){ if(!ser_ok) ser_init(); while((inp(COM1+5)&0x20)==0){} outp(COM1,(unsigned char)c); }
static void ser_str(const char *s){ while(*s) ser_ch(*s++); }

static void io_delay(int n){ while(n-->0) (void)inp(SB_RESET); }

static int g_resetbyte = -1;
/* returns 1 = reset OK (0xAA), 0 = wrong byte, -1 = timeout (no DSP) */
static int dsp_reset(void)
{
    long t;
    outp(SB_RESET, 1);
    io_delay(50);
    outp(SB_RESET, 0);
    for (t = 0; t < 200000L; t++) {
        if (inp(SB_RSTAT) & 0x80) {
            g_resetbyte = inp(SB_READ) & 0xFF;
            return (g_resetbyte == 0xAA) ? 1 : 0;
        }
    }
    return -1;
}

static int dsp_write(unsigned char b)
{
    long t;
    for (t = 0; t < 200000L; t++)
        if ((inp(SB_WRITE) & 0x80) == 0) { outp(SB_WRITE, b); return 1; }
    return 0;
}

static int dsp_read(void)
{
    long t;
    for (t = 0; t < 200000L; t++)
        if (inp(SB_RSTAT) & 0x80) return inp(SB_READ) & 0xFF;
    return -1;
}

static FILE *g_rf = 0;
static void out2(const char *s) { printf("%s", s); ser_str(s); if (g_rf) { fputs(s, g_rf); fflush(g_rf); } }

int main(void)
{
    char line[80];
    int r, maj, min, fm;

    g_rf = fopen("C:\\SBREPORT.TXT", "w");

    out2("SBTEST: probing SoundBlaster DSP at 220h...\n");

    r = dsp_reset();
    if (r == 1) {
        dsp_write(0xE1);              /* DSP Get Version */
        maj = dsp_read();
        min = dsp_read();
        sprintf(line, "SB DSP: RESET OK (AA), version %d.%02d  => SBEMUL DIGITAL ALIVE\n", maj, min);
        out2(line);
    } else if (r == 0) {
        sprintf(line, "SB DSP: reset returned 0x%02X, not 0xAA (0xFF=open bus => SBEMUL gone)\n", g_resetbyte);
        out2(line);
    } else {
        out2("SB DSP: NO RESPONSE at 220h  => SBEMUL digital is DISABLED/gone\n");
    }

    /* also show who owns the FM port 388h (00 = VOPL3, 06/FF = SBEMUL/open) */
    fm = inp(0x388) & 0xFF;
    sprintf(line, "FM status port 388h reads 0x%02X (00=VOPL3 owns it)\n", fm);
    out2(line);

    if (g_rf) fclose(g_rf);
    return 0;
}
