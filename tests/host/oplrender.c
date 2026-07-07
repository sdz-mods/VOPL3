/* oplrender - host-side OPL3 render harness around Nuked OPL3.
 *
 * Modes:
 *   oplrender selftest                     run built-in checks, exit 0/1
 *   oplrender tone <out.wav> [seconds]     ADLIBTST test-tone patch (golden ref)
 *   oplrender sine <out.wav> [seconds]     carrier-only 440 Hz sine patch
 *   oplrender script <in.txt> <out.wav>    lines: <time_ms> <reg_hex> <val_hex>
 *   oplrender dro <in.dro> <out.wav>       DOSBox Raw OPL capture (v1/v2)
 *
 * Output: 16-bit stereo PCM WAV at 48000 Hz.
 * Nuked OPL3 is LGPL 2.1 (separate module); this file is part of the
 * VOPL3 project test suite.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "opl3.h"

#define RATE 48000

static opl3_chip chip;

/* ---------- WAV buffer ---------- */
static int16_t *wavbuf = NULL;
static size_t wavlen = 0, wavcap = 0;   /* in stereo frames */
static uint64_t cursample = 0;

static void render_to(uint64_t sample)
{
    if (sample <= cursample) return;
    size_t need = (size_t)(sample);
    if (need > wavcap) {
        wavcap = need + RATE;
        wavbuf = (int16_t *)realloc(wavbuf, wavcap * 2 * sizeof(int16_t));
        if (!wavbuf) { fprintf(stderr, "out of memory\n"); exit(2); }
    }
    OPL3_GenerateStream(&chip, wavbuf + cursample * 2,
                        (uint32_t)(sample - cursample));
    cursample = sample;
    if (cursample > wavlen) wavlen = cursample;
}

static void wav_write(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t datalen = (uint32_t)(wavlen * 4), u32;
    uint16_t u16;
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(2); }
    fwrite("RIFF", 1, 4, f);
    u32 = 36 + datalen;        fwrite(&u32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    u32 = 16;                  fwrite(&u32, 4, 1, f);
    u16 = 1;                   fwrite(&u16, 2, 1, f); /* PCM */
    u16 = 2;                   fwrite(&u16, 2, 1, f); /* stereo */
    u32 = RATE;                fwrite(&u32, 4, 1, f);
    u32 = RATE * 4;            fwrite(&u32, 4, 1, f);
    u16 = 4;                   fwrite(&u16, 2, 1, f); /* block align */
    u16 = 16;                  fwrite(&u16, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&datalen, 4, 1, f);
    fwrite(wavbuf, 4, wavlen, f);
    fclose(f);
    printf("wrote %s (%.2f s, %u frames)\n", path,
           (double)wavlen / RATE, (unsigned)wavlen);
}

/* ---------- analysis ---------- */
static double an_rms(size_t from, size_t to)
{
    double acc = 0; size_t n = 0, i;
    for (i = from; i < to && i < wavlen; i++, n++) {
        double s = wavbuf[i * 2];
        acc += s * s;
    }
    return n ? sqrt(acc / n) : 0.0;
}

/* autocorrelation pitch estimate on left channel, [50..1000] Hz */
static double an_pitch(size_t from, size_t to)
{
    size_t n = (to < wavlen ? to : wavlen) - from, lag;
    size_t minlag = RATE / 1000, maxlag = RATE / 50;
    double best = -1; size_t bestlag = 0;
    if (n < maxlag * 2) return 0;
    for (lag = minlag; lag <= maxlag; lag++) {
        double acc = 0, e1 = 0, e2 = 0; size_t i;
        for (i = 0; i < n - lag; i++) {
            double a = wavbuf[(from + i) * 2], b = wavbuf[(from + i + lag) * 2];
            acc += a * b; e1 += a * a; e2 += b * b;
        }
        if (e1 > 0 && e2 > 0) {
            double r = acc / sqrt(e1 * e2);
            if (r > best) { best = r; bestlag = lag; }
        }
    }
    return bestlag ? (double)RATE / bestlag : 0;
}

/* ---------- patches ---------- */
static void wr(uint16_t reg, uint8_t v) { OPL3_WriteReg(&chip, reg, v); }

/* exact register sequence ADLIBTST.COM plays in the guest */
static void patch_adlibtst_tone(void)
{
    wr(0x20, 0x01); wr(0x40, 0x10); wr(0x60, 0xF0); wr(0x80, 0x77);
    wr(0x23, 0x01); wr(0x43, 0x00); wr(0x63, 0xF0); wr(0x83, 0x77);
    wr(0xA0, 0x98); wr(0xB0, 0x31);                 /* key on, ~309.5 Hz */
}

/* carrier-only sine, fnum 580 block 4 -> 440.0 Hz */
static void patch_sine440(void)
{
    wr(0x20, 0x01); wr(0x40, 0x3F); wr(0x60, 0xF0); wr(0x80, 0x77);
    wr(0x23, 0x01); wr(0x43, 0x00); wr(0x63, 0xF0); wr(0x83, 0x77);
    wr(0xC0, 0x31);                                 /* additive, both out */
    wr(0xA0, 0x44); wr(0xB0, 0x32);                 /* key on */
}

/* ---------- script mode ---------- */
static int run_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    while (fgets(line, sizeof line, f)) {
        double ms; unsigned reg, val;
        if (line[0] == '#' || line[0] == ';') continue;
        if (sscanf(line, "%lf %x %x", &ms, &reg, &val) == 3) {
            render_to((uint64_t)(ms * RATE / 1000.0));
            wr((uint16_t)reg, (uint8_t)val);
        }
    }
    fclose(f);
    render_to(cursample + RATE / 2);    /* 0.5 s tail */
    return 0;
}

/* ---------- DRO mode ---------- */
static int run_dro(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[26];
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "DBRAWOPL", 8)) {
        fprintf(stderr, "not a DRO file\n"); fclose(f); return 1;
    }
    {
        uint32_t vmaj = hdr[8] | (hdr[9] << 8) | (hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
        double ms = 0;
        if (vmaj == 2) {                 /* --- v2.0 --- */
            unsigned char h2[14], codemap[128];
            uint32_t pairs, i;
            uint8_t shortd, longd, maplen;
            if (fread(h2, 1, 14, f) != 14) goto bad;
            /* h2: verMinor(4) lenPairs(4) lenMS(4) hw(1) fmt(1) -> need 6 more */
            pairs = h2[4] | (h2[5] << 8) | (h2[6] << 16) | ((uint32_t)h2[7] << 24);
            {
                unsigned char h3[4];
                if (fread(h3, 1, 4, f) != 4) goto bad;  /* compression, shortDelay, longDelay, maplen */
                shortd = h3[1]; longd = h3[2]; maplen = h3[3];
            }
            if (maplen > 128 || fread(codemap, 1, maplen, f) != maplen) goto bad;
            for (i = 0; i < pairs; i++) {
                int code = fgetc(f), val = fgetc(f);
                if (code < 0 || val < 0) break;
                if (code == shortd)      ms += val + 1;
                else if (code == longd)  ms += (val + 1) * 256.0;
                else {
                    uint16_t reg = codemap[code & 0x7F] | ((code & 0x80) ? 0x100 : 0);
                    render_to((uint64_t)(ms * RATE / 1000.0));
                    wr(reg, (uint8_t)val);
                }
            }
        } else {                         /* --- v0.1/1.0 --- */
            unsigned char h1[8];
            int bank = 0, c;
            if (fread(h1, 1, 8, f) != 8) goto bad;   /* lenMS, lenBytes */
            /* hardware type: 1 byte, but some writers used 4; peek */
            c = fgetc(f);
            if (c == 0) { int p = fgetc(f); int q = fgetc(f);
                          if (!(p == 0 && q == 0)) { ungetc(q, f); ungetc(p, f); ungetc(c, f); fgetc(f); } }
            for (;;) {
                int reg = fgetc(f), v;
                if (reg < 0) break;
                if (reg == 0x00)      { v = fgetc(f); ms += v + 1; }
                else if (reg == 0x01) { int lo = fgetc(f), hi = fgetc(f);
                                        ms += (lo | (hi << 8)) + 1; }
                else if (reg == 0x02) bank = 0;
                else if (reg == 0x03) bank = 0x100;
                else {
                    if (reg == 0x04) reg = fgetc(f);
                    v = fgetc(f); if (v < 0) break;
                    render_to((uint64_t)(ms * RATE / 1000.0));
                    wr((uint16_t)(reg | bank), (uint8_t)v);
                }
            }
        }
        fclose(f);
        render_to(cursample + RATE / 2);
        return 0;
    }
bad:
    fprintf(stderr, "truncated DRO header\n"); fclose(f); return 1;
}

/* ---------- selftest ---------- */
static int selftest(void)
{
    int fail = 0;
    double v;

    /* 1: silence after reset */
    OPL3_Reset(&chip, RATE);
    wavlen = cursample = 0;
    render_to(RATE / 5);
    v = an_rms(0, wavlen);
    printf("silence RMS      = %8.1f  (expect < 3)      %s\n", v, v < 3 ? "OK" : "FAIL");
    if (v >= 3) fail = 1;

    /* 2: pure sine pitch */
    OPL3_Reset(&chip, RATE);
    wavlen = cursample = 0;
    patch_sine440();
    render_to(RATE);
    v = an_pitch(RATE / 4, RATE);
    printf("sine pitch       = %8.1f  (expect 440 +-5%%)  %s\n", v,
           (v > 418 && v < 462) ? "OK" : "FAIL");
    if (!(v > 418 && v < 462)) fail = 1;
    v = an_rms(RATE / 4, RATE);
    printf("sine RMS         = %8.1f  (expect > 500)    %s\n", v, v > 500 ? "OK" : "FAIL");
    if (v <= 500) fail = 1;

    /* 3: ADLIBTST tone: pitch ~309.5 Hz fundamental, audible level */
    OPL3_Reset(&chip, RATE);
    wavlen = cursample = 0;
    patch_adlibtst_tone();
    render_to(RATE);
    v = an_pitch(RATE / 4, RATE);
    printf("adlibtst pitch   = %8.1f  (expect 309 +-6%%)  %s\n", v,
           (v > 291 && v < 328) ? "OK" : "FAIL");
    if (!(v > 291 && v < 328)) fail = 1;
    v = an_rms(RATE / 4, RATE);
    printf("adlibtst RMS     = %8.1f  (expect > 500)    %s\n", v, v > 500 ? "OK" : "FAIL");
    if (v <= 500) fail = 1;

    /* 4: key-off actually releases */
    wr(0xB0, 0x11);
    render_to(cursample + RATE);
    v = an_rms(wavlen - RATE / 10, wavlen);
    printf("post-keyoff RMS  = %8.1f  (expect < 50)     %s\n", v, v < 50 ? "OK" : "FAIL");
    if (v >= 50) fail = 1;

    printf(fail ? "SELFTEST FAILED\n" : "SELFTEST PASSED\n");
    return fail;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s selftest | tone out.wav [sec] | sine out.wav [sec]"
                        " | script in.txt out.wav | dro in.dro out.wav\n", argv[0]);
        return 2;
    }
    OPL3_Reset(&chip, RATE);

    if (!strcmp(argv[1], "selftest")) return selftest();

    if (!strcmp(argv[1], "tone") || !strcmp(argv[1], "sine")) {
        double sec = (argc > 3) ? atof(argv[3]) : 2.0;
        if (argc < 3) { fprintf(stderr, "need output file\n"); return 2; }
        if (argv[1][0] == 't') patch_adlibtst_tone(); else patch_sine440();
        render_to((uint64_t)(sec * RATE));
        wr(0xB0, 0x11);                       /* key off */
        render_to(cursample + RATE / 2);
        wav_write(argv[2]);
        printf("pitch=%.1f Hz rms=%.0f\n", an_pitch(RATE / 4, (size_t)(sec * RATE)),
               an_rms(RATE / 4, (size_t)(sec * RATE)));
        return 0;
    }
    if (!strcmp(argv[1], "script") && argc >= 4) {
        if (run_script(argv[2])) return 1;
        wav_write(argv[3]);
        return 0;
    }
    if (!strcmp(argv[1], "dro") && argc >= 4) {
        if (run_dro(argv[2])) return 1;
        wav_write(argv[3]);
        return 0;
    }
    fprintf(stderr, "bad arguments\n");
    return 2;
}
