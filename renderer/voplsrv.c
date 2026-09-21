/* voplsrv.exe - OPL3 + MIDI renderer for VOPL3.VXD (Win9x GUI-subsystem app)
 *
 * Opens \\.\VOPL3 and services two things the VxD traps for DOS programs:
 *   - OPL3 FM: polls the register-write ring, feeds the OPL3 emulator (one
 *     per build: Nuked OPL3, Nuked-OPL3-fast or DOSBox's DBOPL), plays via
 *     waveOut (KMIXER mixes it with SBEMUL's digital audio - no sound-driver
 *     changes needed);
 *   - MPU-401 MIDI: drains the captured MIDI byte stream and re-emits it via
 *     midiOut to the MIDI Mapper (or a chosen device), so DOS MIDI reaches
 *     ANY installed synth/hardware instead of SBEMUL's fixed kernel GS synth.
 *
 * It is a GUI-subsystem app (WinMain, no console) so it runs silently in the
 * background - only errors pop up a message box. It owns one hidden window,
 * created purely so Windows can talk to it: WM_ENDSESSION stops the audio
 * before the system tears the process down (avoid blue screen).
 *
 * Build (Open Watcom, Win32, runs on Win98): see build.ps1.
 * Nuked OPL3 / Nuked-OPL3-fast (opl3.c) are LGPL 2.1 and shipped as a
 * separate module; DOSBox's DBOPL (dbopl/) is GPL v2 or later, which makes
 * the DBOPL build (vopldb.exe) GPL as a whole.
 */
#include <windows.h>
#include <mmsystem.h>
#ifdef VOPL3_DBOPL
#include "dbopl_glue.h"
#else
#include "opl3.h"
#endif
#include "vopl3ipc.h"      /* shared status/control contract with VOPLCFG.EXE */

#define RATE      48000        /* default + maximum output rate; rate=     */
#ifndef FRAMES                 /* overridable (-dFRAMES=...) for buffering */
#define FRAMES    480          /* per buffer at 48 kHz: 10 ms; at other    */
#endif                         /* rates scaled to the same duration (see   */
                               /* set_rate). Also sizes the buffers.       */
                               /* -d override: experiments on troublesome */
                               /* sound drivers                            */
#ifndef NBUF
#define NBUF      16           /* default buffer count: ~160 ms (rides out */
#endif                         /* scheduling gaps while DOOM hogs the CPU; */
                               /* music latency is fine)                   */
#define NBUF_MAX  96           /* [renderer] buffer= upper bound (~960 ms) */
#define BUFMS     (FRAMES / (RATE / 1000))   /* ms per buffer              */
#define DRAINMAX  8192         /* max writes drained per poll      */
#define MIDIMAX   4096         /* max MIDI bytes drained per poll   */

#define IOCTL_VOPL3_DRAIN        0x1000
#define IOCTL_VOPL3_STAT         0x1001
#define IOCTL_VOPL3_MIDI_DRAIN   0x1002
#define IOCTL_VOPL3_MIDI_ENABLE  0x1003
#define IOCTL_VOPL3_MIDI_VM_GONE 0x1004
#define IOCTL_VOPL3_FM_ENABLE    0x1005

#ifndef FILE_FLAG_DELAYED_ERROR
#define FILE_FLAG_DELAYED_ERROR 0x10000000
#endif

static HANDLE    hvxd;
static HWAVEOUT  hwo;
static WAVEHDR   hdr[NBUF_MAX];
static short     bufs[NBUF_MAX][FRAMES * 2];
static DWORD     drainbuf[DRAINMAX];
static long      gain256 = 512;    /* output gain, 256 = 1.0x; set from INI */
static UINT      volume_pct = 200; /* FM volume percent; set from INI         */
static int       nbuf = NBUF;      /* buffers in flight; [renderer] buffer=  */
static DWORD     rate = RATE;      /* output sample rate; [renderer] rate=    */
static DWORD     frames = FRAMES;  /* frames per buffer at that rate          */
static int       prio_mode;        /* 0=auto 1=realtime 2=normal; priority=  */
static HANDLE    hev;              /* waveOut buffer-completion event         */
static DWORD     idleclose_ms;     /* [renderer] idleclose=; 0 = never close  */
static int       out_closed;       /* output device released while FM idle    */

static HMIDIOUT  hmidi;            /* MPU-401 MIDI output, open only while used */
static UINT      midi_dev = (UINT)MIDI_MAPPER;  /* device id; set from INI  */
static int       midi_on;          /* mode includes MIDI (registry Midi=1)    */
static int       fm_on;            /* VOPL3 plays FM (registry Fm == 1)       */
static DWORD     fm_mode;          /* registry Fm as read (see reg_dword)     */
static DWORD     midi_last;        /* GetTickCount of last MIDI byte          */
static DWORD     midi_total;       /* total MIDI bytes fed to the synth       */
static BYTE      midibuf[MIDIMAX]; /* raw MIDI bytes drained from the VxD    */
static int       realtime;         /* renderer currently at realtime priority */

/* Published status block (shared memory) the GUI reads; NULL if it couldn't
 * be created (the renderer runs fine either way). See status_publish(). */
static VOPL3_STATUS *g_stat;
static HANDLE        g_statmap;
static UINT          g_msg_reload;  /* RegisterWindowMessage(VOPL3_MSG_RELOAD) */
static UINT          g_msg_panic;   /* RegisterWindowMessage(VOPL3_MSG_PANIC)  */

/* ---- the OPL3 emulator: one per build ----
 * Everything below talks to the chip only through these three calls.
 * Register writes get the real chip's minimum spacing (~40 us) with every
 * backend: Nuked's OPL3_WriteRegBuffered does it (see render_buffer), and the
 * DBOPL glue paces them the same way (see dbopl/dbopl_glue.cpp for why that
 * matters - some music depends on the gaps). */
#ifdef VOPL3_DBOPL
#define BACKEND_ID 2                /* built against DOSBox's DBOPL */
static void chip_reset(DWORD r)                  { dbopl_reset(r); }
static void chip_write(WORD reg, BYTE val)       { dbopl_write(reg, val); }
static void chip_generate(short *dst, DWORD n)   { dbopl_generate(dst, n); }
#else
#ifdef VOPL3_FAST
#define BACKEND_ID 1                /* built against nuked-opl3-fast */
#else
#define BACKEND_ID 0                /* built against nuked-opl3 (reference) */
#endif
static opl3_chip chip;
static void chip_reset(DWORD r)                  { OPL3_Reset(&chip, r); }
static void chip_write(WORD reg, BYTE val)       { OPL3_WriteRegBuffered(&chip, reg, val); }
static void chip_generate(short *dst, DWORD n)   { OPL3_GenerateStream(&chip, dst, n); }
#endif

/* ---- FM volume boost ----
 * Nuked-OPL3 reproduces the OPL3's digital output level exactly, which
 * sounds quiet next to SBEMUL's digital SFX. Boost after synthesis (the
 * emulator cores stay untouched / bit-exact).
 * Configured in VOPL3.INI next to the exe:
 * [renderer] volume=<percent>, default 200, clamped to 400. */
static void apply_gain(short *p, int n)
{
    long v;
    int  i;
    if (gain256 == 256) return;
    for (i = 0; i < n; i++) {
        v = ((long)p[i] * gain256) >> 8;
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        p[i] = (short)v;
    }
}

/* VOPL3.INI next to the exe */
static void ini_path(char *ini)
{
    DWORD n = GetModuleFileName(NULL, ini, MAX_PATH - 12);
    while (n && ini[n - 1] != '\\') n--;
    lstrcpy(ini + n, "VOPL3.INI");
}

/* Buffers keep their ~10 ms length at every rate: FRAMES is the count at
 * 48 kHz, scaled down here (rounding down - 220 frames = 9.98 ms at 22050),
 * so everything counted in buffers (idle detection, buffer=, the wake
 * cadence) means the same time at any rate. */
static void set_rate(DWORD r)
{
    rate   = r;
    frames = (DWORD)FRAMES * r / RATE;
}

/* [renderer] rate=<hz>: the output sample rate - one of the standard rates
 * below (default 48000); anything else is treated as 48000. Nuked would
 * resample to any rate, but there is no point supporting anything besides
 * the standard ones. Read ONCE, at startup - not in load_settings, which
 * also runs on every control-panel reload: a new rate needs a chip reset,
 * which would wipe the chip's registers (the game's instrument setup)
 * mid-game. With the Nuked cores the OPL3 engine's CPU cost is the same at
 * any rate: they emulate the chip at its native 49716 Hz and always resample
 * to this rate (DBOPL instead computes directly at this rate). Worth
 * changing e.g. on 44.1k-native hardware, where rate=44100 spares KMIXER a
 * 48->44.1 conversion and the CPU time it takes. */
static void load_rate(void)
{
    static const UINT ok[] = { 11025, 16000, 22050, 32000, 44100, 48000 };
    char ini[MAX_PATH];
    UINT r, i;
    ini_path(ini);
    r = GetPrivateProfileInt("renderer", "rate", RATE, ini);
    for (i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
        if (r == ok[i]) { set_rate(r); return; }
    set_rate(RATE);
}

static void load_settings(void)
{
    char ini[MAX_PATH];
    UINT pct;
    ini_path(ini);
    pct = GetPrivateProfileInt("renderer", "volume", 200, ini);
    if (pct > 400) pct = 400;
    volume_pct = pct;
    gain256 = ((long)pct << 8) / 100;

    /* [midi] device: 0xFFFF (default) = MIDI Mapper (follows the user's
     * control-panel choice); 0,1,2,... = a specific midiOut device index. */
    { UINT d = GetPrivateProfileInt("midi", "device", 0xFFFF, ini);
      midi_dev = (d == 0xFFFF) ? (UINT)MIDI_MAPPER : d; }

    /* [renderer] buffer=<total ms> (default 160, clamped 40-960): deeper
     * buffering rides out sound drivers that deliver buffer completions
     * late or in bursts (audible as a snippet repeating ~every half second)
     * at the cost of FM latency. Buffer SIZE stays 10 ms - only the count
     * changes - so the wake cadence is unaffected. Applied at start only:
     * the buffers are already queued with the device on a live reload. */
    { UINT ms = GetPrivateProfileInt("renderer", "buffer", NBUF * BUFMS, ini);
      if (ms < 4 * BUFMS)        ms = 4 * BUFMS;
      if (ms > NBUF_MAX * BUFMS) ms = NBUF_MAX * BUFMS;
      if (!hwo) nbuf = ms / BUFMS; }

    /* [renderer] priority=auto|realtime|normal (default auto):
     *   auto     = realtime only while producing audio (FM playing or MIDI
     *              flowing), normal at idle;
     *   realtime = hold realtime the whole time (the pre-A04 behaviour -
     *              escape hatch for systems that regressed on auto);
     *   normal   = never raise (diagnostic).
     * Applies live on a control-panel reload. */
    { char ps[16];
      GetPrivateProfileString("renderer", "priority", "auto", ps, sizeof(ps), ini);
      if      (!lstrcmpi(ps, "realtime")) prio_mode = 1;
      else if (!lstrcmpi(ps, "normal"))   prio_mode = 2;
      else                                prio_mode = 0; }

    /* [renderer] idleclose=<seconds> (default 0 = off, clamped 5-3600):
     * after this long with the FM chip silent, RELEASE the output device
     * (waveOutReset/Unprepare/Close) instead of continuing to stream zeroed
     * buffers; reopen on the first OPL register write. Idle-skip alone stops
     * the synthesis but keeps the stream - and therefore the sound card's DMA
     * engine keeps running forever.
     * Applies live on a control-panel reload. */
    { UINT s = GetPrivateProfileInt("renderer", "idleclose", 0, ini);
      if (s && s < 5) s = 5;
      if (s > 3600)   s = 3600;
      idleclose_ms = s * 1000; }
}

/* What VOPL3 handles is an install-time choice stored in the registry as two
 * values (set by the installer):
 *   Fm   1 = VOPL3 plays FM, 0 = FM left to SBEMUL,
 *        2 = ports 388-38B left free (SBEMUL steered away, VOPL3 doesn't trap)
 *   Midi 1 = VOPL3 handles MIDI, 0 = MIDI left to SBEMUL
 * Read it in USER MODE here - NOT in the VxD - so the kernel driver stays free
 * of registry/string code. The VxD traps a port range only when we ask, so
 * ports VOPL3 doesn't handle are never touched - SBEMUL, or whatever else
 * uses them, would stop working without them. A missing Fm value means FM on
 * (installs from before these choices existed). */
static DWORD reg_dword(const char *name, DWORD def)
{
    HKEY  hk;
    DWORD val = def, cb = sizeof(val), type = 0;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\VOPL3", 0, KEY_READ, &hk)
            != ERROR_SUCCESS)
        return def;
    if (RegQueryValueEx(hk, name, NULL, &type, (BYTE *)&val, &cb) != ERROR_SUCCESS)
        val = def;
    RegCloseKey(hk);
    return val;
}

/* ===================== MPU-401 MIDI bridge =====================
 * The VxD's MPU-401 UART trap captures the DOS program's raw MIDI byte stream
 * into a ring; we drain it and emit MIDI messages via midiOut to the MIDI
 * Mapper (or a chosen device) - so DOS MIDI can go to ANY installed synth or
 * hardware, not just SBEMUL's fixed kernel GS synth. Parser handles running
 * status, SysEx, and system-realtime bytes interleaved mid-message. */
static BYTE run_status;            /* current MIDI running status 0x80..0xEF */
static BYTE mmsg[3];
static int  mneed, mhave;
static BYTE sysex[1024];
static int  sxlen, in_sysex;

static int midi_datacount(BYTE status)
{
    switch (status & 0xF0) {
        case 0xC0: case 0xD0: return 1;   /* program change, channel pressure */
        default:              return 2;   /* note/CC/bend/aftertouch          */
    }
}

static void midi_short(BYTE s, BYTE d1, BYTE d2)
{
    if (hmidi) midiOutShortMsg(hmidi, (DWORD)s | ((DWORD)d1<<8) | ((DWORD)d2<<16));
}

static void midi_long(BYTE *p, int n)
{
    MIDIHDR h;
    if (!hmidi) return;
    ZeroMemory(&h, sizeof(h));
    h.lpData = (char *)p; h.dwBufferLength = h.dwBytesRecorded = (DWORD)n;
    if (midiOutPrepareHeader(hmidi, &h, sizeof(h)) == MMSYSERR_NOERROR) {
        midiOutLongMsg(hmidi, &h, sizeof(h));
        midiOutUnprepareHeader(hmidi, &h, sizeof(h));
    }
}

/* feed one raw MPU-401 byte through the MIDI parser */
static void midi_feed(BYTE b)
{
    if (b >= 0xF8) { midi_short(b, 0, 0); return; }   /* realtime: 1 byte, */
                                                      /* keeps running status */
    if (in_sysex) {
        if (b == 0xF7)      { sysex[sxlen++] = b; midi_long(sysex, sxlen); in_sysex = 0; }
        else if (b < 0x80)  { if (sxlen < (int)sizeof(sysex)) sysex[sxlen++] = b; }
        else                { midi_long(sysex, sxlen); in_sysex = 0; midi_feed(b); }
        return;
    }
    if (b == 0xF0)          { run_status = 0; in_sysex = 1; sxlen = 0; sysex[sxlen++] = b; return; }
    if (b >= 0x80 && b <= 0xEF) { run_status = b; mmsg[0] = b; mhave = 0; mneed = midi_datacount(b); return; }
    if (b >= 0xF1 && b <= 0xF7) { run_status = 0; return; }   /* system common */
    /* data byte */
    if (!run_status) return;
    mmsg[1 + mhave] = b; mhave++;
    if (mhave >= mneed) { midi_short(run_status, mmsg[1], mneed == 2 ? mmsg[2] : 0); mhave = 0; }
}

/* Open the MIDI synth lazily (on the first byte since it went quiet), NOT at
 * startup and NOT continuously.
 *
 * When to release it: when the DOS box that was playing MIDI closes (the VxD
 * flags that via IOCTL_VOPL3_MIDI_VM_GONE) - so the synth survives in-game
 * musical gaps of any length without losing its channel state. */
#define MIDI_CLOSE_MS 30000        /* untracked sources: release after this */
#define MIDI_RT_MS    2000         /* "MIDI recently flowing" window (priority) */

static void midi_open(void)
{
    int wasrt = realtime;
    if (wasrt) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    }
    if (midiOutOpen(&hmidi, midi_dev, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        hmidi = NULL;
    if (wasrt) {
        SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    }
}

static void midi_close(void)
{
    if (hmidi) { midiOutReset(hmidi); midiOutClose(hmidi); hmidi = NULL; }
}

/* Drain the VxD MIDI ring; open the synth on activity, release it when the
 * game's DOS box closes (VM gone), or after the timer for an untracked
 * source. Called every loop wake (whether or not the synth is open). */
static void service_midi(void)
{
    DWORD ret = 0, i;
    if (!midi_on) return;
    if (!DeviceIoControl(hvxd, IOCTL_VOPL3_MIDI_DRAIN, NULL, 0,
                         midibuf, sizeof(midibuf), &ret, NULL))
        ret = 0;
    if (ret) {
        if (!hmidi) midi_open();
        for (i = 0; i < ret; i++) midi_feed(midibuf[i]);
        midi_total += ret;
        midi_last = GetTickCount();
        return;
    }
    if (hmidi) {                               /* quiet right now - release? */
        DWORD st[2] = { 0, 0 }, r = 0;         /* [0] VM gone, [1] DOS box */
        DeviceIoControl(hvxd, IOCTL_VOPL3_MIDI_VM_GONE, NULL, 0,
                        st, sizeof(st), &r, NULL);
        if (r < 8) st[1] = 0;                  /* older VxD: untracked */
        if (st[0] ||
            (!st[1] && (GetTickCount() - midi_last) > MIDI_CLOSE_MS))
            midi_close();
    }
}

/* Realtime priority is held only while actually producing audio - FM playing
 * OR MIDI flowing - so a CPU-bound DOS game can't starve either into
 * choppiness; at the desktop (both idle) we drop to normal so a spin/deadlock
 * can't wedge the machine. */
static void go_realtime(void)
{
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    realtime = 1;
}

static void go_normal(void)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    realtime = 0;
}

/* ---- status block for the GUI ----
 * Publish a small shared-memory snapshot the VOPLCFG control panel reads. This
 * is best-effort: if the mapping can't be created the renderer runs exactly as
 * before. The GUI treats a missing mapping (or a stale `tick`) as "renderer not
 * running" and never depends on it. */
static void status_init(void)
{
    g_statmap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                  0, sizeof(VOPL3_STATUS), VOPL3_STATUS_NAME);
    if (!g_statmap) return;
    g_stat = (VOPL3_STATUS *)MapViewOfFile(g_statmap, FILE_MAP_WRITE, 0, 0,
                                           sizeof(VOPL3_STATUS));
    if (!g_stat) return;
    ZeroMemory(g_stat, sizeof(*g_stat));
    g_stat->magic   = VOPL3_STATUS_MAGIC;
    g_stat->ver     = VOPL3_STATUS_VER;
    g_stat->rev     = VOPL3_REV_DWORD;
    g_stat->backend = BACKEND_ID;
}

static void status_publish(int active)
{
    if (!g_stat) return;
    g_stat->midi_on    = midi_on;
    g_stat->fm_mode    = fm_mode;
    g_stat->rate       = rate;
    g_stat->synth_open = hmidi ? 1 : 0;
    g_stat->midi_dev   = midi_dev;
    g_stat->realtime   = realtime;
    g_stat->active     = active;
    g_stat->out_open   = hwo ? 1 : 0;
    g_stat->volume     = volume_pct;
    g_stat->midi_bytes = midi_total;
    g_stat->frames++;
    g_stat->tick       = GetTickCount();
}

/* ---- timestamped write scheduling ----
 * Ring entries are (ms15 << 17) | (reg << 8) | data. Applying a whole
 * drain's worth of writes at one instant deletes any note shorter than the
 * drain interval: key-on and key-off land with ZERO generated samples in
 * between, so the envelope opens and closes silently (measured: a 10 ms-
 * bucketed replay deletes a sizeable fraction of 6 ms staccato notes).
 * Instead, drained writes are queued with a target SAMPLE position and
 * applied mid-buffer by render_buffer(), which generates in slices around
 * them - restoring the real-time spacing the ISA bus gave them.
 *
 * Mapping ms -> samples: the first event after silence anchors at the
 * current stream position; each subsequent event advances the anchor by its
 * ms delta (wrap-safe, deltas only). Guards: overdue events apply now;
 * a >2 s gap re-anchors (also covers the 32.7 s timestamp wrap); a runaway
 * lead (device clock slower than the ms clock) is compressed so latency
 * stays bounded.
 * The ms -> samples step carries its remainder (anch_frac, in thousandths of
 * a sample) from event to event. At 48 kHz a ms is exactly 48 samples, but at
 * e.g. 44.1 kHz it is 44.1: rounding each delta on its own would lose 0.1
 * sample per event, the anchor would drift steadily behind the stream, and
 * within seconds every write would land "overdue" - collapsing them back to
 * per-drain timing, the very thing the timestamps are here to prevent. */
#define PQMAX    8192              /* power of two */
#define MAXAHEAD (frames * nbuf * 2)

static DWORD pq_time[PQMAX];       /* absolute target sample */
static WORD  pq_reg[PQMAX];
static BYTE  pq_val[PQMAX];
static DWORD pq_head, pq_tail;
static DWORD stream_pos;           /* samples generated since start */
static DWORD anch_t15, anch_smp, anch_frac, last_tgt;
static int   anch_ok;

/* drain the VxD ring into the queue; returns entries drained */
static DWORD drain_events(void)
{
    DWORD ret = 0, n, i;
    if (!DeviceIoControl(hvxd, IOCTL_VOPL3_DRAIN, NULL, 0,
                         drainbuf, sizeof(drainbuf), &ret, NULL))
        return 0;
    n = ret >> 2;
    for (i = 0; i < n; i++) {
        DWORD e    = drainbuf[i];
        DWORD t15  = e >> 17;
        DWORD tgt;
        DWORD frac = 0;                     /* re-anchored: no remainder */
        if (!anch_ok) {
            tgt = stream_pos;
            anch_ok = 1;
        } else {
            DWORD dms = (t15 - anch_t15) & 0x7FFF;
            if (dms > 2000) {                          /* long gap / wrap */
                tgt = stream_pos;
            } else {
                DWORD acc = dms * rate + anch_frac;    /* milli-samples; fits:
                                                        * 2000 * 48000 < 2^32 */
                tgt  = anch_smp + acc / 1000;
                frac = acc % 1000;
                if ((long)(tgt - stream_pos) < 0) {    /* overdue: apply now */
                    tgt = stream_pos;           frac = 0;
                } else if (tgt - stream_pos > MAXAHEAD) { /* drift: compress */
                    tgt = stream_pos + frames;  frac = 0;
                }
            }
        }
        if ((long)(tgt - last_tgt) < 0) { tgt = last_tgt; frac = 0; } /* keep order */
        anch_t15 = t15; anch_smp = tgt; anch_frac = frac; last_tgt = tgt;

        if (pq_tail - pq_head >= PQMAX) {              /* full: apply oldest */
            chip_write(pq_reg[pq_head & (PQMAX - 1)], pq_val[pq_head & (PQMAX - 1)]);
            pq_head++;
        }
        pq_time[pq_tail & (PQMAX - 1)] = tgt;
        pq_reg [pq_tail & (PQMAX - 1)] = (WORD)((e >> 8) & 0x1FF);
        pq_val [pq_tail & (PQMAX - 1)] = (BYTE)(e & 0xFF);
        pq_tail++;
    }
    return n;
}

/* generate one buffer (frames), applying queued writes at their sample offsets.
 * With Nuked, writes go through OPL3_WriteRegBuffered, NOT OPL3_WriteReg: buffered
 * writes get the chip's real minimum spacing (2 chip samples, ~40 us -
 * the ISA-bus pacing every real OPL3 ever saw). Slamming a whole burst of
 * writes onto one chip instant with OPL3_WriteReg races the envelope/phase
 * logic - notes drop and tones come out wrong (proven by A/B against a
 * DOSBox DRO capture: identical stream, WriteReg = broken, Buffered =
 * matches DOSBox note-for-note). */
static void render_buffer(short *dst)
{
    DWORD done = 0;
    while (done < frames) {
        DWORD n = frames - done;
        while (pq_head != pq_tail &&
               (long)(pq_time[pq_head & (PQMAX - 1)] - stream_pos) <= 0) {
            chip_write(pq_reg[pq_head & (PQMAX - 1)], pq_val[pq_head & (PQMAX - 1)]);
            pq_head++;
        }
        if (pq_head != pq_tail) {
            DWORD due = pq_time[pq_head & (PQMAX - 1)] - stream_pos;
            if (due < n) n = due;
        }
        chip_generate(dst + done * 2, n);
        done       += n;
        stream_pos += n;
    }
}

/* ---- idle skip ----
 * The emulator computes every operator even when the chip is silent, so an
 * idle renderer would cost as much CPU as a playing one. OPL3 silence is
 * digital zero, so gate on OUTPUT, not on register-write inactivity
 * (a sustained note can sound forever without further writes):
 * after IDLE_AFTER consecutive near-silent buffers with nothing drained from
 * the ring, stop synthesizing and emit zeroed buffers instead. Resume on the
 * first drained register write - games start with detection/init writes well
 * before the first audible note, so no music onset is ever clipped.
 *
 * "Near-silent", not "all zero": a keyed-off OPL3 does NOT settle to exact
 * digital zero - Nuked leaves a tiny DC/rounding residual (a few units per
 * sounding operator; ~+-36 worst case with all 18 channels released). An
 * exact-zero test therefore never fires once any note has played, so the
 * renderer would synthesize forever at full CPU. Treat anything below
 * SILENCE_EPS - far under -50 dBFS, inaudible, and well below any real
 * sounding note's level - as silence. Checked on the raw chip output before
 * the volume boost, so the threshold is independent of the volume setting. */
#define IDLE_AFTER  48                     /* ~0.5 s of near-silent buffers */
#define SILENCE_EPS 64                     /* |sample| below this = silent   */

static int buf_silent(const short *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] > SILENCE_EPS || p[i] < -SILENCE_EPS) return 0;
    return 1;
}

/* ---- output device open / release ----
 * Split out of WinMain so the idle-close path ([renderer] idleclose=) can
 * cycle the device with exactly the same sequence used at startup and at
 * shutdown.The MIDI synth is deliberately untouched by
 * either: it is a different device with its own lifecycle (see service_midi),
 * and FM idle says nothing about whether MIDI is flowing. */
static int audio_open(void)
{
    WAVEFORMATEX wf;
    int i;

    if (hwo) return 1;

    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 2;
    wf.nSamplesPerSec  = rate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = 4;
    wf.nAvgBytesPerSec = rate * 4;
    wf.cbSize          = 0;

    if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD)hev, 0, CALLBACK_EVENT)
            != MMSYSERR_NOERROR) {
        hwo = NULL;
        return 0;
    }

    /* prime all buffers */
    for (i = 0; i < nbuf; i++) {
        hdr[i].lpData         = (char *)bufs[i];
        hdr[i].dwBufferLength = frames * 4;
        hdr[i].dwFlags        = 0;
        hdr[i].dwLoops        = 0;
        waveOutPrepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
        drain_events();
        render_buffer(bufs[i]);
        apply_gain(bufs[i], frames * 2);
        waveOutWrite(hwo, &hdr[i], sizeof(WAVEHDR));
    }
    return 1;
}

/* Returns 1 if the device is now genuinely ours no longer. The close is
 * checked: waveOutClose fails (MMSYSERR_STILLPLAYING) if any header is still
 * queued, which also catches a waveOutUnprepareHeader that refused. Nulling
 * hwo regardless would leak the handle and publish "output released" while we
 * still held the device - the one state the idleclose= test must be able to
 * trust. On failure we keep hwo and the caller stays open. */
static int audio_release(void)
{
    int i;
    if (!hwo) return 1;
    waveOutReset(hwo);                     /* returns all queued buffers */
    for (i = 0; i < nbuf; i++)
        waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
    if (waveOutClose(hwo) != MMSYSERR_NOERROR)
        return 0;
    hwo = NULL;
    return 1;
}

/* ---- clean shutdown ----
 * Stop the audio while the process is still healthy. */
static void audio_stop(void)
{
    audio_release();
    if (hmidi) {
        midiOutReset(hmidi);               /* all-notes-off on the synth */
        midiOutClose(hmidi);
        hmidi = NULL;
    }
    if (hvxd && hvxd != INVALID_HANDLE_VALUE) {
        CloseHandle(hvxd);
        hvxd = NULL;
    }
    if (g_stat)   { UnmapViewOfFile(g_stat); g_stat = NULL; }
    if (g_statmap){ CloseHandle(g_statmap);  g_statmap = NULL; }
    timeEndPeriod(1);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    /* Live control from the VOPLCFG GUI (registered messages, so compared as
     * variables, not switch cases). */
    if (g_msg_reload && msg == g_msg_reload) {
        UINT olddev = midi_dev;
        load_settings();                 /* re-read VOPL3.INI: volume + device */
        /* Volume applies instantly (gain256 is read live per buffer). If the
         * MIDI device changed while the synth is open, release it so the next
         * MIDI byte reopens on the newly chosen device - no restart needed. */
        if (hmidi && midi_dev != olddev) midi_close();
        return 0;
    }
    if (g_msg_panic && msg == g_msg_panic) {
        if (hmidi) midiOutReset(hmidi);  /* all notes off; keep the synth open */
        return 0;
    }
    switch (msg) {
    case WM_QUERYENDSESSION:
        return TRUE;                       /* no objection to shutdown */
    case WM_ENDSESSION:
        if (wp) {                          /* the session IS ending: after we
                                            * return, the process can be killed
                                            * at any moment - stop audio NOW */
            audio_stop();
            ExitProcess(0);
        }
        return 0;
    case WM_CLOSE:                         /* "End Task" from Close Program */
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    static WNDCLASS wc;                    /* static: zero-initialized */
    int   i;
    int   idle     = 0;                    /* skipping synthesis (chip silent) */
    DWORD silence  = 0;                    /* consecutive near-silent buffers  */
    int   idle_timed = 0;                  /* idle_since holds a valid stamp   */
    DWORD idle_since = 0;                  /* tick the current idle run began  */
    DWORD retry_at   = 0;                  /* next reopen attempt (tick)       */
    DWORD retry_ms   = 0;                  /* reopen backoff, 0 = none pending */

    hvxd = CreateFile("\\\\.\\VOPL3", 0, 0, NULL, 0, FILE_FLAG_DELAYED_ERROR, NULL);
    if (hvxd == INVALID_HANDLE_VALUE) {
        MessageBox(NULL, "Cannot open \\\\.\\VOPL3 - is VOPL3.VXD loaded?\n"
                         "(Reboot after installing, or check the install.)",
                   "VOPL3 renderer", MB_OK | MB_ICONSTOP);
        return 1;
    }

    /* Hidden top-level window: exists only to receive WM_ENDSESSION (clean
     * audio stop at shutdown - see header comment) and WM_CLOSE (End Task).
     * Never shown. */
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "VOPLSRV";
    RegisterClass(&wc);
    CreateWindow("VOPLSRV", "VOPLSRV", WS_OVERLAPPED,
                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                 NULL, NULL, hInst, NULL);

    /* Registered messages the VOPLCFG GUI POSTs to apply INI changes live and
     * to send an all-notes-off. Same name resolves to the same value in both. */
    g_msg_reload = RegisterWindowMessage(VOPL3_MSG_RELOAD);
    g_msg_panic  = RegisterWindowMessage(VOPL3_MSG_PANIC);

    /* Win9x's default scheduler tick is ~55 ms, so a polling loop that falls
     * back to Sleep() starves the audio buffers in bursts even when the CPU is
     * idle. Ask for 1 ms timer granularity and, below, drive the refill loop
     * off a waveOut completion event so we wake exactly when a buffer frees. */
    timeBeginPeriod(1);

    load_settings();
    load_rate();
    status_init();
    chip_reset(rate);

    /* MPU-401 MIDI bridge - only if VOPL3 handles MIDI. Tell the VxD to start
     * trapping 0x330/0x331. The MIDI synth itself is opened lazily
     * (service_midi) only when a game actually sends MIDI, and released when
     * the game's DOS box closes. With MIDI left to SBEMUL we never touch the
     * MIDI ports or open a synth at all. */
    midi_on = reg_dword("Midi", 0) ? 1 : 0;
    fm_mode = reg_dword("Fm",   1);
    fm_on   = (fm_mode == 1);
    if (midi_on) {
        DWORD ret = 0;
        DeviceIoControl(hvxd, IOCTL_VOPL3_MIDI_ENABLE, NULL, 0, NULL, 0, &ret, NULL);
    }

    hev = CreateEvent(NULL, FALSE, FALSE, NULL);

    /* FM - only if VOPL3 plays it: trap 388-38B and open the audio output.
     * Otherwise neither happens, ever: the OPL ports stay SBEMUL's or free,
     * and the loop below runs as if the output had been released by
     * idleclose=, with nothing that can reopen it - so there is no audio
     * stream (no DMA, no synthesis), only MIDI servicing. */
    if (fm_on) {
        DWORD ret = 0;
        int   ok;
        DeviceIoControl(hvxd, IOCTL_VOPL3_FM_ENABLE, NULL, 0, NULL, 0, &ret, NULL);
        /* A driver may refuse an unusual rate=. Nothing has played yet, so
         * fall back to the default rather than give up. */
        ok = audio_open();
        if (!ok && rate != RATE) {
            set_rate(RATE);
            chip_reset(rate);
            ok = audio_open();
        }
        if (!ok) {
            MessageBox(NULL, "waveOutOpen failed - no usable Windows audio output.",
                       "VOPL3 renderer", MB_OK | MB_ICONSTOP);
            return 1;
        }
    } else {
        out_closed = 1;
        idle       = 1;
    }

    /* Realtime priority is managed DYNAMICALLY in the loop below (see
     * go_realtime/go_normal): held while FM plays or MIDI flows, dropped to
     * normal when both are idle. Realtime only matters to out-run a CPU-bound
     * DOS game so audio doesn't stutter; at the Windows desktop there is
     * nothing to out-run. [renderer] priority= overrides this (see
     * load_settings). */

    /* steady state: the event fires each time waveOut finishes a buffer; wake,
     * drain the newest register writes, regenerate every freed buffer and
     * requeue it. The timeout backstops a stalled/bursty sound driver: 100 ms
     * normally, but 10 ms while the MIDI synth is open - MIDI forwarding is
     * paced by these wakes, and on drivers whose buffer completions arrive in
     * bursts a 100 ms cadence audibly clumps the MIDI stream.
     * MsgWaitForMultipleObjects (QS_ALLINPUT covers sent messages too) also
     * wakes for window messages, so the hidden window receives
     * WM_ENDSESSION/WM_CLOSE without a second thread.
     * While the output is released (idleclose=) there are no completions at
     * all, so the timeout is the only thing driving the loop: shorten it to
     * 10 ms there, or up to 100 ms of it would be added to the delay before
     * we even notice the register write that must reopen the device.
     * With neither FM nor MIDI to handle (FM ports left free, MIDI left to
     * SBEMUL) there is nothing to poll: wake once a second, only to keep the
     * status block fresh for VOPLCFG. */
    for (;;) {
        DWORD wr = MsgWaitForMultipleObjects(1, &hev, FALSE,
                                             (!fm_on && !midi_on)  ? 1000 :
                                             (out_closed || hmidi) ? 10 : 100,
                                             QS_ALLINPUT);
        if (wr == WAIT_OBJECT_0 + 1) {
            MSG m;
            while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) {    /* End Task -> clean exit */
                    audio_stop();
                    return 0;
                }
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        }
        /* MPU-401 MIDI: drain + send every wake (~10 ms) regardless of FM
         * buffer state, so MIDI plays even while the OPL chip is idle. Opens
         * the synth on demand and releases it after it goes quiet. */
        service_midi();
        /* Drain the OPL ring on EVERY wake too - not only below, when a
         * buffer has completed. Some sound drivers stall their buffer
         * completions for long stretches (seen in the wild while a DOS box
         * executes); if draining waited on them, the VxD ring would back up
         * (tail frozen, writes lost) and idle-exit / the realtime raise
         * would stall with it. Draining here keeps capture independent of
         * the driver's pace - which is what this loop's timeout is for. */
        {
            DWORD nw = drain_events();
            if (nw) {                          /* chip touched: (re)start */
                idle    = 0;
                silence = 0;
            }
        }

        /* ---- idleclose= : reopen ----
         * The chip was touched (or the knob was turned off live) while the
         * device was released - take it back. Writes drained above were
         * queued against the frozen stream_pos and the anchor was cleared on
         * release, so the first of them targets "now" and the rest keep their
         * real spacing from there; audio_open()'s priming render applies them.
          */
        if (fm_on && out_closed && (!idle || !idleclose_ms)) {
            DWORD now = GetTickCount();
            if (!retry_ms || (long)(now - retry_at) >= 0) {
                if (audio_open()) {
                    out_closed = 0;
                    retry_ms   = 0;
                } else {
                    retry_ms = retry_ms ? (retry_ms < 5000 ? retry_ms * 2 : 5000)
                                        : 100;
                    retry_at = now + retry_ms;
                }
            }
        }

        /* No completions to service while the device is released. */
        for (i = 0; !out_closed && i < nbuf; i++) {
            if (hdr[i].dwFlags & WHDR_DONE) {
                DWORD n = drain_events();
                if (n) {                       /* chip touched: (re)start */
                    idle    = 0;
                    silence = 0;
                }
                if (!idle) {
                    int silent;
                    render_buffer(bufs[i]);
                    silent = buf_silent(bufs[i], frames * 2);  /* raw output */
                    apply_gain(bufs[i], frames * 2);
                    if (n == 0 && pq_head == pq_tail && silent) {
                        if (++silence >= IDLE_AFTER) idle = 1;
                    } else if (n == 0) {
                        silence = 0;           /* still sounding (decay etc.) */
                    }
                } else {
                    ZeroMemory(bufs[i], frames * 4);  /* true silence while idle */
                    stream_pos += frames;             /* time passes while idle */
                }
                hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(hwo, &hdr[i], sizeof(WAVEHDR));
            }
        }
        /* ---- idleclose= : release ----
         * Idle-skip has already stopped the synthesis, but the stream (and
         * with it the card's DMA engine) keeps running on zeroed buffers. If
         * the user asked for it, drop the device once the chip has been
         * silent for idleclose= seconds. Timed from the start of the idle
         * run, so a game that pauses briefly never reaches it.
         * Clearing anch_ok makes the first write after the reopen re-anchor
         * at the current stream_pos: the position stops advancing while the
         * device is gone, so without this a gap under the drain code's 2 s
         * re-anchor threshold would schedule that write far in the future
         * (it self-corrects via the MAXAHEAD clamp, but only after mangling
         * the spacing of the first notes back).
         * MIDI is unaffected - different device, and it may well be playing
         * while the FM chip is silent. */
        if (idle && !idle_timed) { idle_since = GetTickCount(); idle_timed = 1; }
        if (!idle) idle_timed = 0;
        if (idleclose_ms && idle && idle_timed && !out_closed &&
                GetTickCount() - idle_since >= idleclose_ms) {
            if (audio_release()) {
                out_closed = 1;
                anch_ok    = 0;
                retry_ms   = 0;
            } else {
                /* Could not let go of it. Stay open, and wait a full
                 * idleclose interval before trying again rather than
                 * hammering waveOutClose on every wake. */
                idle_timed = 0;
            }
        }

        /* Priority per [renderer] priority= mode. auto: realtime while FM
         * plays (chip not idle) OR MIDI is actively flowing (a byte within
         * MIDI_RT_MS); normal otherwise. Note auto tracks MIDI ACTIVITY, not
         * whether the synth is open - during an in-game gap the synth stays
         * open but there is nothing to keep up with, so we drop to normal.
         * out_closed is checked as well as idle: while the device is
         * released, idle stays 0 from the moment writes arrive until the
         * reopen succeeds, and a reopen that keeps failing must not leave us
         * holding realtime with no audio output at all. */
        if (prio_mode == 1)      { if (!realtime) go_realtime(); }
        else if (prio_mode == 2) { if (realtime)  go_normal();   }
        else {
            int busy = (!idle && !out_closed)
                    || (GetTickCount() - midi_last) < MIDI_RT_MS;
            if (busy && !realtime)      go_realtime();
            else if (!busy && realtime) go_normal();
        }
        status_publish(!idle && !out_closed);
    }
    /* not reached */
}
