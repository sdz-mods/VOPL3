/* C interface to ymfm's YMF262 (OPL3) for the VOPL3 renderer.
 *
 * The chip runs at its native rate (14.31818 MHz / 288 = 49716 Hz, as Nuked
 * OPL3 does); the output rate is reached by linear interpolation between
 * native samples. Outputs A+C go left and B+D right, as Nuked mixes them,
 * halved to Nuked's level so the FM volume setting means the same with
 * every backend.
 *
 * ymfm's own ymf262::generate clamps its mix to 16 bits at ymfm's level,
 * which is twice Nuked's: halved afterwards, that would clip at half the
 * loudness Nuked and DBOPL clip at (most of all with the FM volume below
 * 200%, where the boost doesn't clip there first).
 * So the mix is taken before that clamp (see vopl3_ymf262), halved, and
 * only then clamped to 16 bits - the same headroom as the other backends.
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

/* ymf262 with a generate that skips the final clamp16: the mix stays 32-bit */
class vopl3_ymf262 : public ymfm::ymf262
{
public:
    vopl3_ymf262(ymfm::ymfm_interface &intf) : ymfm::ymf262(intf) { }
    void generate_unclamped(output_data *output)
    {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        m_fm.output(output->clear(), 0, 32767, fm_engine::ALL_CHANNELS);
    }
};

static ymfm::ymfm_interface g_intf;
static vopl3_ymf262 *g_chip;
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

static int clamp16(int v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return v;
}

/* one chip sample, applying each queued write when its time comes; the
 * result is at Nuked's level and within 16 bits */
static void chip_sample(int *lr)
{
    ymfm::ymf262::output_data out;
    while (g_qh != g_qt && (long)(g_q[g_qh & (QSIZE - 1)].t - g_now) <= 0) {
        apply(g_q[g_qh & (QSIZE - 1)].reg, g_q[g_qh & (QSIZE - 1)].val);
        g_qh++;
    }
    g_chip->generate_unclamped(&out);
    g_now++;
    lr[0] = clamp16((out.data[0] + out.data[2]) / 2);
    lr[1] = clamp16((out.data[1] + out.data[3]) / 2);
}

extern "C" void ymfm_reset(unsigned rate)
{
    if (!g_chip) g_chip = new vopl3_ymf262(g_intf);
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
        /* both ends are within 16 bits, so the point between them is too;
         * a 15-bit fraction keeps the product within a 32-bit long even for
         * a full-scale step (65535 * 32767 < 2^31) */
        long f = (long)((g_pos & 0xFFFF) >> 1);
        out[2 * i]     = (short)(g_prev[0] + (int)(((long)(g_cur[0] - g_prev[0]) * f) >> 15));
        out[2 * i + 1] = (short)(g_prev[1] + (int)(((long)(g_cur[1] - g_prev[1]) * f) >> 15));
        g_pos += g_step;
        while (g_pos >= 0x10000) {
            g_pos -= 0x10000;
            g_prev[0] = g_cur[0]; g_prev[1] = g_cur[1];
            chip_sample(g_cur);
        }
    }
}
