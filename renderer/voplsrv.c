/* voplsrv.exe - OPL3 + MIDI renderer for VOPL3.VXD (Win9x GUI-subsystem app)
 *
 * Opens \\.\VOPL3 and services two things the VxD traps for DOS programs:
 *   - OPL3 FM: polls the register-write ring, feeds Nuked OPL3, plays via
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
 * Nuked OPL3 (opl3.c) is LGPL 2.1 and shipped as a separate module.
 */
#include <windows.h>
#include <mmsystem.h>
#include "opl3.h"

#define RATE      48000
#define FRAMES    480          /* per buffer: 10 ms                */
#define NBUF      16           /* ~160 ms of buffering (rides out  */
                               /* scheduling gaps while DOOM hogs  */
                               /* the CPU); music latency is fine  */
#define DRAINMAX  8192         /* max writes drained per poll      */
#define MIDIMAX   4096         /* max MIDI bytes drained per poll   */

#define IOCTL_VOPL3_DRAIN       0x1000
#define IOCTL_VOPL3_STAT        0x1001
#define IOCTL_VOPL3_MIDI_DRAIN  0x1002
#define IOCTL_VOPL3_MIDI_ENABLE 0x1003

#ifndef FILE_FLAG_DELAYED_ERROR
#define FILE_FLAG_DELAYED_ERROR 0x10000000
#endif

static opl3_chip chip;
static HANDLE    hvxd;
static HWAVEOUT  hwo;
static WAVEHDR   hdr[NBUF];
static short     bufs[NBUF][FRAMES * 2];
static DWORD     drainbuf[DRAINMAX];
static long      gain256 = 512;    /* output gain, 256 = 1.0x; set from INI */

static HMIDIOUT  hmidi;            /* MPU-401 MIDI output (0 if unavailable) */
static UINT      midi_dev = (UINT)MIDI_MAPPER;  /* device id; set from INI  */
static int       midi_on;          /* user enabled FM+MIDI (registry Midi=1) */
static BYTE      midibuf[MIDIMAX]; /* raw MIDI bytes drained from the VxD    */

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

static void load_settings(void)
{
    char ini[MAX_PATH];
    UINT pct;
    DWORD n = GetModuleFileName(NULL, ini, sizeof(ini) - 12);
    while (n && ini[n - 1] != '\\') n--;
    lstrcpy(ini + n, "VOPL3.INI");
    pct = GetPrivateProfileInt("renderer", "volume", 200, ini);
    if (pct > 400) pct = 400;
    gain256 = ((long)pct << 8) / 100;

    /* [midi] device: 0xFFFF (default) = MIDI Mapper (follows the user's
     * control-panel choice); 0,1,2,... = a specific midiOut device index. */
    { UINT d = GetPrivateProfileInt("midi", "device", 0xFFFF, ini);
      midi_dev = (d == 0xFFFF) ? (UINT)MIDI_MAPPER : d; }
}

/* MIDI on/off is an install-time choice stored in the registry (set by the
 * installer per the FM-only vs FM+MIDI prompt). Read it in USER MODE here -
 * NOT in the VxD - so the kernel driver stays free of registry/string code,
 * and so FM-only mode never even attempts to touch the MPU-401 ports, which
 * the VxD would otherwise grab before SBEMUL at boot and thereby break
 * SBEMUL's MIDI. */
static int midi_enabled(void)
{
    HKEY  hk;
    DWORD val = 0, cb = sizeof(val), type = 0;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\VOPL3", 0, KEY_READ, &hk)
            != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueEx(hk, "Midi", NULL, &type, (BYTE *)&val, &cb) != ERROR_SUCCESS)
        val = 0;
    RegCloseKey(hk);
    return val ? 1 : 0;
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

static void drain_midi(void)
{
    DWORD ret = 0, i;
    if (!hmidi) return;
    if (DeviceIoControl(hvxd, IOCTL_VOPL3_MIDI_DRAIN, NULL, 0,
                        midibuf, sizeof(midibuf), &ret, NULL))
        for (i = 0; i < ret; i++) midi_feed(midibuf[i]);
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
 * stays bounded. */
#define PQMAX    8192              /* power of two */
#define SPMS     (RATE / 1000)     /* samples per ms */
#define MAXAHEAD (FRAMES * NBUF * 2)

static DWORD pq_time[PQMAX];       /* absolute target sample */
static WORD  pq_reg[PQMAX];
static BYTE  pq_val[PQMAX];
static DWORD pq_head, pq_tail;
static DWORD stream_pos;           /* samples generated since start */
static DWORD anch_t15, anch_smp, last_tgt;
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
        DWORD e   = drainbuf[i];
        DWORD t15 = e >> 17;
        DWORD tgt;
        if (!anch_ok) {
            tgt = stream_pos;
            anch_ok = 1;
        } else {
            DWORD dms = (t15 - anch_t15) & 0x7FFF;
            if (dms > 2000) {                          /* long gap / wrap */
                tgt = stream_pos;
            } else {
                tgt = anch_smp + dms * SPMS;
                if ((long)(tgt - stream_pos) < 0)      /* overdue: apply now */
                    tgt = stream_pos;
                else if (tgt - stream_pos > MAXAHEAD)  /* clock drift: compress */
                    tgt = stream_pos + FRAMES;
            }
        }
        if ((long)(tgt - last_tgt) < 0) tgt = last_tgt;   /* keep order */
        anch_t15 = t15; anch_smp = tgt; last_tgt = tgt;

        if (pq_tail - pq_head >= PQMAX) {              /* full: apply oldest */
            OPL3_WriteRegBuffered(&chip, pq_reg[pq_head & (PQMAX - 1)],
                                         pq_val[pq_head & (PQMAX - 1)]);
            pq_head++;
        }
        pq_time[pq_tail & (PQMAX - 1)] = tgt;
        pq_reg [pq_tail & (PQMAX - 1)] = (WORD)((e >> 8) & 0x1FF);
        pq_val [pq_tail & (PQMAX - 1)] = (BYTE)(e & 0xFF);
        pq_tail++;
    }
    return n;
}

/* generate FRAMES frames, applying queued writes at their sample offsets.
 * Writes go through OPL3_WriteRegBuffered, NOT OPL3_WriteReg: buffered
 * writes get the chip's real minimum spacing (2 chip samples, ~40 us -
 * the ISA-bus pacing every real OPL3 ever saw). Slamming a whole burst of
 * writes onto one chip instant with OPL3_WriteReg races the envelope/phase
 * logic - notes drop and tones come out wrong (proven by A/B against a
 * DOSBox DRO capture: identical stream, WriteReg = broken, Buffered =
 * matches DOSBox note-for-note). */
static void render_buffer(short *dst)
{
    DWORD done = 0;
    while (done < FRAMES) {
        DWORD n = FRAMES - done;
        while (pq_head != pq_tail &&
               (long)(pq_time[pq_head & (PQMAX - 1)] - stream_pos) <= 0) {
            OPL3_WriteRegBuffered(&chip, pq_reg[pq_head & (PQMAX - 1)],
                                         pq_val[pq_head & (PQMAX - 1)]);
            pq_head++;
        }
        if (pq_head != pq_tail) {
            DWORD due = pq_time[pq_head & (PQMAX - 1)] - stream_pos;
            if (due < n) n = due;
        }
        OPL3_GenerateStream(&chip, dst + done * 2, n);
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

/* ---- clean shutdown ----
 * Stop the audio while the process is still healthy. */
static void audio_stop(void)
{
    int i;
    if (hwo) {
        waveOutReset(hwo);                 /* returns all queued buffers */
        for (i = 0; i < NBUF; i++)
            waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
        waveOutClose(hwo);
        hwo = NULL;
    }
    if (hmidi) {
        midiOutReset(hmidi);               /* all-notes-off on the synth */
        midiOutClose(hmidi);
        hmidi = NULL;
    }
    if (hvxd && hvxd != INVALID_HANDLE_VALUE) {
        CloseHandle(hvxd);
        hvxd = NULL;
    }
    timeEndPeriod(1);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
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
    WAVEFORMATEX wf;
    HANDLE       hev;
    int   i;
    int   idle    = 0;                     /* skipping synthesis (chip silent) */
    DWORD silence = 0;                     /* consecutive near-silent buffers  */

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

    /* Win9x's default scheduler tick is ~55 ms, so a polling loop that falls
     * back to Sleep() starves the audio buffers in bursts even when the CPU is
     * idle. Ask for 1 ms timer granularity and, below, drive the refill loop
     * off a waveOut completion event so we wake exactly when a buffer frees. */
    timeBeginPeriod(1);

    /* A CPU-bound DOS game (e.g. DOOM) pins the CPU and starves this user-mode
     * renderer, which is what makes the OPL3 audio choppy. Run at realtime
     * priority so we preempt the game the instant a buffer needs refilling.
     * This is safe: the loop below blocks on the waveOut event and only runs a
     * few ms per 10 ms buffer, so it never hogs the CPU when idle. */
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    load_settings();
    OPL3_Reset(&chip, RATE);

    /* MPU-401 MIDI bridge - only if the user chose FM+MIDI at install. Tell
     * the VxD to start trapping 0x330/0x331, then open MIDI out (non-fatal:
     * if the open fails, FM still works and MIDI just isn't routed). In
     * FM-only mode we never touch the MIDI ports or open a synth at all. */
    midi_on = midi_enabled();
    if (midi_on) {
        DWORD ret = 0;
        DeviceIoControl(hvxd, IOCTL_VOPL3_MIDI_ENABLE, NULL, 0, NULL, 0, &ret, NULL);
        if (midiOutOpen(&hmidi, midi_dev, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            hmidi = NULL;
    }

    hev = CreateEvent(NULL, FALSE, FALSE, NULL);

    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 2;
    wf.nSamplesPerSec  = RATE;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = 4;
    wf.nAvgBytesPerSec = RATE * 4;
    wf.cbSize          = 0;

    if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD)hev, 0, CALLBACK_EVENT)
            != MMSYSERR_NOERROR) {
        MessageBox(NULL, "waveOutOpen failed - no usable Windows audio output.",
                   "VOPL3 renderer", MB_OK | MB_ICONSTOP);
        return 1;
    }

    /* prime all buffers */
    for (i = 0; i < NBUF; i++) {
        hdr[i].lpData         = (char *)bufs[i];
        hdr[i].dwBufferLength = FRAMES * 4;
        hdr[i].dwFlags        = 0;
        hdr[i].dwLoops        = 0;
        waveOutPrepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
        drain_events();
        render_buffer(bufs[i]);
        apply_gain(bufs[i], FRAMES * 2);
        waveOutWrite(hwo, &hdr[i], sizeof(WAVEHDR));
    }

    /* steady state: the event fires each time waveOut finishes a buffer; wake,
     * drain the newest register writes, regenerate every freed buffer and
     * requeue it. The timeout is only a safety net so we still drain the ring
     * if playback ever stalls. MsgWaitForMultipleObjects (QS_ALLINPUT covers
     * sent messages too) also wakes for window messages, so the hidden window
     * receives WM_ENDSESSION/WM_CLOSE without a second thread. */
    for (;;) {
        DWORD wr = MsgWaitForMultipleObjects(1, &hev, FALSE, 100, QS_ALLINPUT);
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
         * buffer state, so MIDI plays even while the OPL chip is idle. */
        drain_midi();
        for (i = 0; i < NBUF; i++) {
            if (hdr[i].dwFlags & WHDR_DONE) {
                DWORD n = drain_events();
                if (n) {                       /* chip touched: (re)start */
                    idle    = 0;
                    silence = 0;
                }
                if (!idle) {
                    int silent;
                    render_buffer(bufs[i]);
                    silent = buf_silent(bufs[i], FRAMES * 2);  /* raw output */
                    apply_gain(bufs[i], FRAMES * 2);
                    if (n == 0 && pq_head == pq_tail && silent) {
                        if (++silence >= IDLE_AFTER) idle = 1;
                    } else if (n == 0) {
                        silence = 0;           /* still sounding (decay etc.) */
                    }
                } else {
                    ZeroMemory(bufs[i], FRAMES * 4);  /* true silence while idle */
                    stream_pos += FRAMES;             /* time passes while idle */
                }
                hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(hwo, &hdr[i], sizeof(WAVEHDR));
            }
        }
    }
    /* not reached */
}
