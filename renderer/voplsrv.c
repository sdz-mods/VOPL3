/* voplsrv.exe - OPL3 renderer for VOPL3.VXD (Win9x, Win32 GUI-subsystem app)
 *
 * Opens \\.\VOPL3, polls the VxD's ring buffer for trapped OPL register
 * writes, feeds them to Nuked OPL3, and plays the result through waveOut.
 * KMIXER software-mixes it with SBEMUL's digital audio, so no changes to
 * the sound driver are needed.
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

#define IOCTL_VOPL3_DRAIN 0x1000
#define IOCTL_VOPL3_STAT  0x1001

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
}

/* returns the number of register writes drained and applied */
static DWORD apply_writes(void)
{
    DWORD ret = 0;
    if (DeviceIoControl(hvxd, IOCTL_VOPL3_DRAIN, NULL, 0,
                        drainbuf, sizeof(drainbuf), &ret, NULL)) {
        DWORD n = ret >> 2, i;
        for (i = 0; i < n; i++) {
            DWORD e = drainbuf[i];
            OPL3_WriteReg(&chip, (unsigned short)(e >> 8), (unsigned char)(e & 0xFF));
        }
        return n;
    }
    return 0;
}

/* ---- idle skip ----
 * The emulator computes every operator even when the chip is silent, so an
 * idle renderer would cost as much CPU as a playing one. OPL3 silence is
 * digital zero, so gate on OUTPUT, not on register-write inactivity
 * (a sustained note can sound forever without further writes):
 * after IDLE_AFTER consecutive all-zero buffers with nothing drained from
 * the ring, stop synthesizing and just requeue the buffers, which by then
 * all hold zeros (IDLE_AFTER >= NBUF guarantees it). Resume on the first
 * drained register write - games start with detection/init writes well
 * before the first audible note, so no music onset is ever clipped. */
#define IDLE_AFTER 48                      /* ~0.5 s of silent buffers */

static int buf_silent(const short *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i]) return 0;
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
    DWORD silence = 0;                     /* consecutive all-zero buffers     */

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
        apply_writes();
        OPL3_GenerateStream(&chip, bufs[i], FRAMES);
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
        for (i = 0; i < NBUF; i++) {
            if (hdr[i].dwFlags & WHDR_DONE) {
                DWORD n = apply_writes();
                if (n) {                       /* chip touched: (re)start */
                    idle    = 0;
                    silence = 0;
                }
                if (!idle) {
                    OPL3_GenerateStream(&chip, bufs[i], FRAMES);
                    apply_gain(bufs[i], FRAMES * 2);
                    if (n == 0 && buf_silent(bufs[i], FRAMES * 2)) {
                        if (++silence >= IDLE_AFTER) idle = 1;
                    } else if (n == 0) {
                        silence = 0;           /* still sounding (decay etc.) */
                    }
                }
                /* if idle: buffer already holds zeros - requeue it as-is */
                hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(hwo, &hdr[i], sizeof(WAVEHDR));
            }
        }
    }
    /* not reached */
}
