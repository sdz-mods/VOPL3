/* C interface to ymfm's YMF262 (OPL3) for the VOPL3 renderer.
 *
 * The chip runs at its native rate (14.31818 MHz / 288 = 49716 Hz, as Nuked
 * OPL3 does); the output rate is reached by linear interpolation between
 * native samples. Outputs A+C go left and B+D right, as Nuked mixes them,
 * halved to Nuked's level so the FM volume setting means the same with
 * every backend.
 *
 * Register writes are PACED, at least 2 chip samples (~40 us) apart, with
 * audio generated in between - the spacing Nuked's OPL3_WriteRegBuffered
 * gives the other renderers and a real OPL3 sees on the ISA bus. ymfm
 * applies every write the moment it gets it, so without this every write
 * the renderer places on the same sample would land at one instant; some
 * music depends on the gaps (see dbopl/dbopl_glue.cpp for the example that
 * showed it). */
#include "ymfm_opl.h"
#include "ymfm_glue.h"

#define OPL3_CLOCK 14318180u
#define SPACING    2                /* chip samples between two writes */
#define QSIZE      8192             /* pending writes (power of two) */

static ymfm::ymfm_interface g_intf;
static ymfm::ymf262 *g_chip;
static unsigned long g_step;        /* native samples per output sample, 16.16 */
static unsigned long g_pos;         /* position between g_prev and g_cur, 16.16 */
static int g_prev[2], g_cur[2];
static unsigned long g_now;         /* chip samples generated so far */
static unsigned long g_last;        /* when the last queued write is applied */
static struct { unsigned long t; unsigned reg; unsigned char val; } g_q[QSIZE];
static unsigned g_qh, g_qt;

static void apply(unsigned reg, unsigned char val)
{
    /* ymf262::write: offset 0/1 = bank 0 address/data, 2/3 = bank 1; the
     * chip itself maps bank 1 onto bank 0 while OPL3 mode is off */
    unsigned bank = (reg >> 8) & 1;
    g_chip->write(bank * 2, (uint8_t)(reg & 0xFF));
    g_chip->write(bank * 2 + 1, val);
}

/* one chip sample, applying each queued write when its time comes */
static void chip_sample(int *lr)
{
    ymfm::ymf262::output_data out;
    while (g_qh != g_qt && (long)(g_q[g_qh & (QSIZE - 1)].t - g_now) <= 0) {
        apply(g_q[g_qh & (QSIZE - 1)].reg, g_q[g_qh & (QSIZE - 1)].val);
        g_qh++;
    }
    g_chip->generate(&out, 1);
    g_now++;
    lr[0] = (out.data[0] + out.data[2]) / 2;
    lr[1] = (out.data[1] + out.data[3]) / 2;
}

static short clamp16(int v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (short)v;
}

extern "C" void ymfm_reset(unsigned rate)
{
    if (!g_chip) g_chip = new ymfm::ymf262(g_intf);
    g_chip->reset();
    g_step = (unsigned long)((double)g_chip->sample_rate(OPL3_CLOCK) * 65536.0 / rate + 0.5);
    g_now = g_last = 0;
    g_qh = g_qt = 0;
    g_pos = 0;
    chip_sample(g_prev);
    chip_sample(g_cur);
}

extern "C" void ymfm_write(unsigned reg, unsigned char val)
{
    unsigned long t = g_last + SPACING;
    if ((long)(t - g_now) < 0) t = g_now;            /* none pending: at once */
    if (g_qt - g_qh >= QSIZE) {                      /* full: apply the oldest */
        apply(g_q[g_qh & (QSIZE - 1)].reg, g_q[g_qh & (QSIZE - 1)].val);
        g_qh++;
    }
    g_q[g_qt & (QSIZE - 1)].t   = t;
    g_q[g_qt & (QSIZE - 1)].reg = reg;
    g_q[g_qt & (QSIZE - 1)].val = val;
    g_qt++;
    g_last = t;
}

extern "C" void ymfm_generate(short *out, unsigned frames)
{
    unsigned i;
    for (i = 0; i < frames; i++) {
        long f = (long)(g_pos & 0xFFFF);
        out[2 * i]     = clamp16(g_prev[0] + (int)(((long)(g_cur[0] - g_prev[0]) * f) >> 16));
        out[2 * i + 1] = clamp16(g_prev[1] + (int)(((long)(g_cur[1] - g_prev[1]) * f) >> 16));
        g_pos += g_step;
        while (g_pos >= 0x10000) {
            g_pos -= 0x10000;
            g_prev[0] = g_cur[0]; g_prev[1] = g_cur[1];
            chip_sample(g_cur);
        }
    }
}
