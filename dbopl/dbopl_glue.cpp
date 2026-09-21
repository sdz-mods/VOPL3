/* C interface to DOSBox's DBOPL, mirroring how DOSBox itself drives it
 * (DBOPL::Handler in dbopl.cpp): InitTables() before Setup(), mono output in
 * OPL2 mode, stereo in OPL3 mode, at most 512 samples per call, and DOSBox's
 * address mapping for the second register bank.
 *
 * Plus one addition: register writes are PACED, at least ~40 us apart, with
 * audio generated in between - the same pacing Nuked's OPL3_WriteRegBuffered
 * gives the other renderers (2 chip samples at 49716 Hz), and the pacing a
 * real OPL3 sees on the ISA bus. DBOPL itself applies every write the moment
 * it gets it (in DOSBox, when writes reach it is up to the rest of the
 * emulator), so without this, every write the renderer places on the same
 * sample would land at one instant.
 * Most music doesn't care, but some depends on the gaps. For example, a
 * register sequence found in some tracks keys voices off with the fastest
 * release, zeroes the rates a few writes later, then keys them on again with
 * attack rate 0 - each voice freezes at whatever level its release reached
 * in between. With no gap that level is far too loud, and the frozen voices
 * come out as a loud buzz for up to half a second (the same writes through
 * Nuked, or through DBOPL with this pacing, freeze them far quieter). */
#include "dbopl.h"
#include "dbopl_glue.h"

namespace DBOPL { void InitTables( void ); }   /* in dbopl.cpp, not in dbopl.h */

#define QSIZE 8192                  /* pending writes (power of two) */

static DBOPL::Chip *g_chip;
static unsigned long g_now;         /* samples generated so far */
static unsigned long g_last;        /* when the last queued write is applied */
static unsigned      g_spacing;     /* minimum samples between two writes */
static struct { unsigned long t; unsigned reg; unsigned char val; } g_q[QSIZE];
static unsigned      g_qh, g_qt;

/* as DOSBox's Handler::Init: the shared wave/envelope tables first (without
 * them every table is zero and the chip is silent), then the chip itself */
extern "C" void dbopl_reset(unsigned rate)
{
    DBOPL::InitTables();
    delete g_chip;
    g_chip = new DBOPL::Chip(true);
    g_chip->Setup(rate);
    g_now = g_last = 0;
    g_qh = g_qt = 0;
    g_spacing = (rate * 40 + 500000) / 1000000;      /* 40 us, rounded */
    if (!g_spacing) g_spacing = 1;
}

static void apply(unsigned reg, unsigned char val)
{
    /* DOSBox's Chip::WriteAddr: a bank-1 register address only reaches bank
     * 1 once OPL3 mode is on (or for 0x105, the register that turns it on);
     * otherwise it aliases bank 0. VOPL3's ring carries the bank bit
     * directly, so apply the same mapping here - at the moment the write
     * takes effect, as in DOSBox. */
    if ((reg & 0x100) && !g_chip->opl3Active && (reg & 0xFF) != 0x05)
        reg &= 0xFF;
    g_chip->WriteReg(reg, val);
}

extern "C" void dbopl_write(unsigned reg, unsigned char val)
{
    unsigned long t = g_last + g_spacing;
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

static short clamp16(Bit32s v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (short)v;
}

static void generate(short *out, unsigned frames)
{
    Bit32s buf[512 * 2];
    while (frames) {
        unsigned n = frames > 512 ? 512 : frames, i;
        if (!g_chip->opl3Active) {
            g_chip->GenerateBlock2(n, buf);                  /* mono */
            for (i = 0; i < n; i++)
                out[2 * i] = out[2 * i + 1] = clamp16(buf[i]);
        } else {
            g_chip->GenerateBlock3(n, buf);                  /* stereo */
            for (i = 0; i < 2 * n; i++)
                out[i] = clamp16(buf[i]);
        }
        out    += 2 * n;
        frames -= n;
    }
}

/* generate, applying each queued write when its time comes */
extern "C" void dbopl_generate(short *out, unsigned frames)
{
    while (frames) {
        unsigned n = frames;
        while (g_qh != g_qt && (long)(g_q[g_qh & (QSIZE - 1)].t - g_now) <= 0) {
            apply(g_q[g_qh & (QSIZE - 1)].reg, g_q[g_qh & (QSIZE - 1)].val);
            g_qh++;
        }
        if (g_qh != g_qt && g_q[g_qh & (QSIZE - 1)].t - g_now < n)
            n = (unsigned)(g_q[g_qh & (QSIZE - 1)].t - g_now);
        generate(out, n);
        out    += 2 * n;
        frames -= n;
        g_now  += n;
    }
}
