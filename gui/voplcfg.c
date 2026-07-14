/* VOPLCFG.EXE - VOPL3 control panel.
 *
 * A small system-tray app that talks to the VOPL3 driver and renderer, split
 * into the package's two functions:
 *   Virtual OPL3       - FM volume (applied LIVE, no restart), renderer/
 *                        backend/FM/priority status, OPL3 debug counters;
 *   MPU-401 MIDI bridge- MIDI output device selection (applied LIVE),
 *                        bridge status, MIDI debug counters.
 * Settings are written to VOPL3.INI (so everything works identically with no
 * GUI running) and the renderer is poked to re-read them.
 *
 * It never depends on VOPL3 running: with no driver it shows "not loaded",
 * with no renderer it shows "not running" and still saves settings.
 *
 * Minimize hides to the system tray; X / Alt+F4 exits. INSTALL.BAT offers a
 * "start with Windows" option (Run-key entry launching "VOPLCFG /tray").
 *
 * Install alongside VOPLSRV.EXE (C:\VOPL3) so it edits the same VOPL3.INI.
 * Build with Open Watcom - see build.ps1.
 */
#define WINVER        0x0400        /* Win98-compatible headers (tray struct etc.) */
#define _WIN32_WINDOWS 0x0400
#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#include "vopl3ipc.h"
#include "resource.h"

#ifndef FILE_FLAG_DELAYED_ERROR
#define FILE_FLAG_DELAYED_ERROR 0x10000000
#endif

#define APP_TITLE "VOPL3 Control Panel"
#define WM_TRAYICON (WM_USER + 100)
#define STALE_MS  3000              /* renderer status older than this = stopped */

enum {
    ID_DEV = 1001, ID_VOL, ID_VOLTXT, ID_APPLY,
    ID_TRAY_OPEN, ID_TRAY_EXIT
};

static HWND   g_hwnd, g_dev, g_vol, g_voltxt, g_msg;
static HWND   g_opl_l1, g_opl_l2, g_opl_s1, g_opl_s2;   /* OPL3 status/stats */
static HWND   g_mid_l1, g_mid_s1;                       /* MIDI status/stats */
static HWND   g_rev;                                    /* bottom revisions  */
static HICON  g_icon, g_icon_sm;    /* 32x32 (window/Alt-Tab) + 16x16 (tray) */
static int    g_tray_visible;
static char   g_tip[128] = "VOPL3";
static char   g_ini[MAX_PATH];

static UINT   g_msg_reload;         /* registered message to the renderer */

/* Last text pushed to each USER-visible surface. On Win9x every USER/GDI call
 * takes the Win16Mutex (USER is 16-bit underneath), and the tray update is a
 * synchronous SendMessage into Explorer - so we only touch them when the text
 * actually CHANGED, keeping mutex traffic near zero during gameplay. */
static char g_p_opl_l1[96], g_p_opl_l2[96], g_p_opl_s1[128], g_p_opl_s2[128];
static char g_p_mid_l1[96], g_p_mid_s1[128], g_p_rev[96], g_p_tip[128];

/* live data sources (each optional) */
static HANDLE        g_vxd = INVALID_HANDLE_VALUE;   /* \\.\VOPL3, read-only  */
static HANDLE        g_map;                          /* renderer status map    */
static VOPL3_STATUS *g_rs;                           /* mapped view (or NULL)  */

/* ------------------------------------------------------------------ helpers */
/* revision DWORD = up to 4 ASCII chars, little-endian (see vopl3ipc.h) */
static void revstr(DWORD v, char *out)   /* out[8] */
{
    memcpy(out, &v, 4);
    out[4] = 0;
    if (out[0] < 0x20 || out[0] > 0x7E) strcpy(out, "?");
}

static void ini_path(void)
{
    char *p;
    GetModuleFileName(NULL, g_ini, sizeof(g_ini));
    p = strrchr(g_ini, '\\');
    strcpy(p ? p + 1 : g_ini, "VOPL3.INI");
}

/* set a control's text only when it changed (and never while hidden in tray) */
static void upd(HWND h, char *prev, int prevsz, const char *s)
{
    if (g_tray_visible) return;
    if (!strcmp(prev, s)) return;
    lstrcpyn(prev, s, prevsz);
    SetWindowText(h, s);
}

/* ------------------------------------------------------------- VxD + status */
/* Open the driver read-only for stats; retried each tick until it appears. */
static void vxd_open(void)
{
    if (g_vxd != INVALID_HANDLE_VALUE) return;
    g_vxd = CreateFile("\\\\.\\VOPL3", 0, 0, NULL, 0, FILE_FLAG_DELAYED_ERROR, NULL);
}

/* Read the VxD STAT ioctl; returns bytes filled (0 if no/failed driver). */
static DWORD vxd_stat(DWORD st[10])
{
    DWORD ret = 0;
    unsigned i;
    for (i = 0; i < 10; i++) st[i] = 0;
    vxd_open();
    if (g_vxd == INVALID_HANDLE_VALUE) return 0;
    if (!DeviceIoControl(g_vxd, IOCTL_VOPL3_STAT, NULL, 0, st, 40, &ret, NULL)) {
        CloseHandle(g_vxd);                 /* driver went away - reopen later */
        g_vxd = INVALID_HANDLE_VALUE;
        return 0;
    }
    return ret;
}

/* Attach to the renderer's status mapping if present. The named object lives as
 * long as either side holds a handle, so once opened we keep it - a renderer
 * that restarts reuses the same block; we tell "running" from "stopped" by the
 * freshness of its tick field, not by the handle. */
static VOPL3_STATUS *rstat(void)
{
    if (!g_rs) {
        g_map = OpenFileMapping(FILE_MAP_READ, FALSE, VOPL3_STATUS_NAME);
        if (g_map) g_rs = (VOPL3_STATUS *)MapViewOfFile(g_map, FILE_MAP_READ, 0, 0,
                                                        sizeof(VOPL3_STATUS));
    }
    if (g_rs && g_rs->magic == VOPL3_STATUS_MAGIC) return g_rs;
    return NULL;
}
static int renderer_live(VOPL3_STATUS *s)
{
    return s && (GetTickCount() - s->tick) < STALE_MS;
}

/* ---------------------------------------------------------------- MIDI list */
/* Combo item 0 = MIDI Mapper (INI value 65535); items 1.. = device index-1. */
static void fill_devices(void)
{
    UINT n = midiOutGetNumDevs(), i;
    int  cur = GetPrivateProfileInt("midi", "device", 0xFFFF, g_ini);
    int  sel = 0;
    SendMessage(g_dev, CB_RESETCONTENT, 0, 0);
    SendMessage(g_dev, CB_ADDSTRING, 0, (LPARAM)"MIDI Mapper (Control Panel default)");
    for (i = 0; i < n; i++) {
        MIDIOUTCAPS moc;
        char line[MAXPNAMELEN + 8];
        if (midiOutGetDevCaps(i, &moc, sizeof(moc)) == MMSYSERR_NOERROR)
            sprintf(line, "%u: %s", i, moc.szPname);
        else
            sprintf(line, "%u: (device %u)", i, i);
        SendMessage(g_dev, CB_ADDSTRING, 0, (LPARAM)line);
        if ((int)i == cur) sel = (int)i + 1;
    }
    if (cur == 0xFFFF) sel = 0;
    SendMessage(g_dev, CB_SETCURSEL, sel, 0);
}

/* --------------------------------------------------------------- system tray */
static void tray_notify(int op)
{
    NOTIFYICONDATA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_icon_sm;
    lstrcpyn(nid.szTip, g_tip, sizeof(nid.szTip));
    Shell_NotifyIcon(op, &nid);
}
static void tray_add(void) { if (!g_tray_visible) { tray_notify(NIM_ADD);    g_tray_visible = 1; } }
static void tray_del(void) { if (g_tray_visible)  { tray_notify(NIM_DELETE); g_tray_visible = 0; } }
static void hide_to_tray(void) { ShowWindow(g_hwnd, SW_HIDE); tray_add(); }
static void show_window(void)
{
    tray_del();      /* g_tray_visible doubles as "window hidden" for refresh() */
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}
static void show_tray_menu(void)
{
    POINT pt;
    HMENU m = CreatePopupMenu();
    AppendMenu(m, MF_STRING, ID_TRAY_OPEN, "Open");
    AppendMenu(m, MF_SEPARATOR, 0, NULL);
    AppendMenu(m, MF_STRING, ID_TRAY_EXIT, "Exit");
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);           /* so it dismisses on click-away */
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

/* --------------------------------------------------------------- actions */
static void apply_settings(void)
{
    int sel = (int)SendMessage(g_dev, CB_GETCURSEL, 0, 0);
    int dev = (sel <= 0) ? 0xFFFF : (sel - 1);
    int vol = (int)SendMessage(g_vol, TBM_GETPOS, 0, 0);
    char num[16];
    HWND r;

    sprintf(num, "%d", dev); WritePrivateProfileString("midi", "device", num, g_ini);
    sprintf(num, "%d", vol); WritePrivateProfileString("renderer", "volume", num, g_ini);

    r = FindWindow(VOPL3_WNDCLASS, NULL);
    if (r) {
        PostMessage(r, g_msg_reload, 0, 0);
        SetWindowText(g_msg, "Applied - saved to VOPL3.INI and reloaded the renderer.");
    } else {
        SetWindowText(g_msg, "Saved to VOPL3.INI. Takes effect when VOPLSRV next starts.");
    }
}

/* --------------------------------------------------------------- refresh */
static void refresh(void)
{
    DWORD st[10];
    DWORD n = vxd_stat(st);
    VOPL3_STATUS *s = rstat();
    int live = renderer_live(s);
    char line[160], drv[8], ren[8];

    /* --- Virtual OPL3 section --- */
    if (live)
        sprintf(line, "Renderer: running      Backend: %s",
                s->backend ? "Nuked-OPL3-fast" : "Nuked OPL3");
    else
        strcpy(line, "Renderer: not running");
    upd(g_opl_l1, g_p_opl_l1, sizeof(g_p_opl_l1), line);

    if (live)
        sprintf(line, "FM: %s      Priority: %s",
                s->active ? "playing" : "idle",
                s->realtime ? "realtime" : "normal");
    else
        strcpy(line, "FM: -      Priority: -");
    upd(g_opl_l2, g_p_opl_l2, sizeof(g_p_opl_l2), line);

    sprintf(line, "ring:  head=%u  tail=%u  lost=%u%s",
            (unsigned)st[0], (unsigned)st[1], (unsigned)st[2],
            st[2] ? "  << OVERRUN" : "");
    upd(g_opl_s1, g_p_opl_s1, sizeof(g_p_opl_s1), line);

    sprintf(line, "I/O:   writes=%u  nonbyte=%u", (unsigned)st[3], (unsigned)st[4]);
    upd(g_opl_s2, g_p_opl_s2, sizeof(g_p_opl_s2), line);

    /* --- MPU-401 MIDI bridge section --- */
    /* Priority is process-wide (one renderer), raised by FM OR MIDI activity;
     * show it here too, annotated with who is holding realtime, so a MIDI-only
     * game doesn't read as "FM idle yet realtime" with no explanation. */
    if (!live)          strcpy(line, "Bridge: -");
    else if (!s->midi_on) strcpy(line, "Bridge: off (FM-only install)");
    else sprintf(line, "Bridge: enabled, synth %s      Priority: %s",
                 s->synth_open ? "open" : "closed",
                 !s->realtime ? "normal" :
                 s->active ? "realtime" : "realtime (held by MIDI)");
    upd(g_mid_l1, g_p_mid_l1, sizeof(g_p_mid_l1), line);

    sprintf(line, "stats: captured=%u  lost=%u%s  uart=%u  sent=%u",
            (unsigned)st[5], (unsigned)st[7], st[7] ? " << OVERRUN" : "",
            (unsigned)st[8], live ? (unsigned)s->midi_bytes : 0u);
    upd(g_mid_s1, g_p_mid_s1, sizeof(g_p_mid_s1), line);

    /* --- bottom line: revisions --- */
    if (n >= 40)  revstr(st[9], drv);
    else if (n)   strcpy(drv, "old");            /* pre-A04 driver: no rev field */
    else          strcpy(drv, "not loaded");
    if (live) revstr(s->rev, ren); else strcpy(ren, "not running");
    sprintf(line, "VOPL3.VXD: %s            VOPLSRV.EXE: %s", drv, ren);
    upd(g_rev, g_p_rev, sizeof(g_p_rev), line);

    /* tray tooltip: Shell_NotifyIcon is a SYNCHRONOUS SendMessage into
     * Explorer's tray window. Under a full-screen DOS game Explorer is starved,
     * so that call can park us for a long time - and if it catches us holding
     * the Win16Mutex, the realtime renderer wedges behind it. Only send it when
     * the text changed (it is static during gameplay) AND the renderer is not
     * at realtime; a deferred update goes out on a later tick. */
    if (n == 0) strcpy(g_tip, "VOPL3: driver not loaded");
    else if (!live) strcpy(g_tip, "VOPL3: renderer stopped");
    else sprintf(g_tip, "VOPL3: %s, FM %s", s->midi_on ? "FM+MIDI" : "FM",
                 s->active ? "playing" : "idle");
    if (g_tray_visible && strcmp(g_tip, g_p_tip) && !(live && s->realtime)) {
        strcpy(g_p_tip, g_tip);
        tray_notify(NIM_MODIFY);
    }
}

/* --------------------------------------------------------------- UI build */
static HWND mk(HWND p, const char *cls, const char *txt, DWORD st,
               int x, int y, int w, int h, int id)
{
    return CreateWindow(cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h,
                        p, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
}

/* One STATIC per text line, 18px tall - Win98's System font clips anything
 * multiline in a short control, so no control here ever holds two lines. */
static void build_ui(HWND w)
{
    /* ============ Virtual OPL3 ============ */
    mk(w, "BUTTON", "Virtual OPL3", BS_GROUPBOX, 8, 6, 388, 226, 0);
    mk(w, "STATIC", "ports 388-38B -> VXD trap -> VOPLSRV (Nuked OPL3)",
       0, 20, 26, 368, 18, 0);
    mk(w, "STATIC", "-> waveOut -> KMIXER -> sound card",
       0, 76, 44, 312, 18, 0);
    mk(w, "STATIC", "Emulates the AdLib/OPL3 FM chip in software;",
       0, 20, 68, 368, 18, 0);
    mk(w, "STATIC", "works for both DOS and Windows programs.",
       0, 20, 86, 368, 18, 0);

    mk(w, "STATIC", "FM volume:", 0, 20, 116, 62, 18, 0);
    g_voltxt = mk(w, "STATIC", "200%", 0, 344, 116, 44, 18, ID_VOLTXT);
    g_vol = mk(w, TRACKBAR_CLASS, "", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
               84, 112, 252, 28, ID_VOL);
    SendMessage(g_vol, TBM_SETRANGE, TRUE, MAKELONG(0, 400));
    SendMessage(g_vol, TBM_SETTICFREQ, 50, 0);

    g_opl_l1 = mk(w, "STATIC", "", 0, 20, 148, 368, 18, 0);
    g_opl_l2 = mk(w, "STATIC", "", 0, 20, 166, 368, 18, 0);
    g_opl_s1 = mk(w, "STATIC", "", 0, 20, 188, 368, 18, 0);
    g_opl_s2 = mk(w, "STATIC", "", 0, 20, 206, 368, 18, 0);

    /* ============ MPU-401 MIDI bridge ============ */
    mk(w, "BUTTON", "MPU-401 MIDI bridge", BS_GROUPBOX, 8, 240, 388, 196, 0);
    mk(w, "STATIC", "ports 330-331 -> VXD trap -> VOPLSRV MIDI parser",
       0, 20, 260, 368, 18, 0);
    mk(w, "STATIC", "-> midiOut -> any installed MIDI device",
       0, 76, 278, 312, 18, 0);
    mk(w, "STATIC", "Bridges MIDI from DOS programs only;",
       0, 20, 302, 368, 18, 0);
    mk(w, "STATIC", "Windows MIDI applications are unaffected.",
       0, 20, 320, 368, 18, 0);

    mk(w, "STATIC", "MIDI output device:", 0, 20, 344, 200, 18, 0);
    g_dev = mk(w, "COMBOBOX", "", WS_BORDER | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
               20, 362, 368, 200, ID_DEV);

    g_mid_l1 = mk(w, "STATIC", "", 0, 20, 392, 368, 18, 0);
    g_mid_s1 = mk(w, "STATIC", "", 0, 20, 410, 368, 18, 0);

    /* ============ bottom ============ */
    mk(w, "BUTTON", "Apply", 0, 157, 444, 90, 26, ID_APPLY);
    g_msg = mk(w, "STATIC", "", 0, 20, 476, 368, 18, 0);
    mk(w, "STATIC", "", SS_ETCHEDHORZ, 8, 498, 388, 2, 0);
    g_rev = mk(w, "STATIC", "", 0, 20, 506, 368, 18, 0);

    fill_devices();
    {
        int vol = GetPrivateProfileInt("renderer", "volume", 200, g_ini);
        char t[16];
        SendMessage(g_vol, TBM_SETPOS, TRUE, vol);
        sprintf(t, "%d%%", vol);
        SetWindowText(g_voltxt, t);
    }
}

static LRESULT CALLBACK WndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g_hwnd = w;
        build_ui(w);
        refresh();
        SetTimer(w, 1, 1000, NULL);
        return 0;
    case WM_TIMER:
        refresh();
        return 0;
    case WM_HSCROLL:
        if ((HWND)lp == g_vol) {
            char t[16];
            sprintf(t, "%d%%", (int)SendMessage(g_vol, TBM_GETPOS, 0, 0));
            SetWindowText(g_voltxt, t);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_APPLY:     apply_settings(); return 0;
        case ID_TRAY_OPEN: show_window(); return 0;
        case ID_TRAY_EXIT: DestroyWindow(w); return 0;
        case ID_DEV:
            /* re-enumerate on every dropdown open, so a synth installed while
             * we're running shows up without a restart; keep the user's
             * current (possibly not-yet-applied) selection when it still
             * exists in the refreshed list */
            if (HIWORD(wp) == CBN_DROPDOWN) {
                int sel = (int)SendMessage(g_dev, CB_GETCURSEL, 0, 0);
                fill_devices();
                if (sel > 0 && sel < (int)SendMessage(g_dev, CB_GETCOUNT, 0, 0))
                    SendMessage(g_dev, CB_SETCURSEL, sel, 0);
            }
            return 0;
        }
        return 0;
    case WM_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK) show_window();
        else if (lp == WM_RBUTTONUP) show_tray_menu();
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) hide_to_tray();   /* minimize -> tray */
        return 0;
    case WM_CLOSE:                                  /* X / Alt+F4 -> exit */
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        tray_del();
        KillTimer(w, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    HWND w;
    MSG msg;
    INITCOMMONCONTROLSEX ic;
    int start_tray = (cmd && strstr(cmd, "/tray") != NULL);

    ic.dwSize = sizeof(ic);
    ic.dwICC  = ICC_BAR_CLASSES;            /* trackbar */
    InitCommonControlsEx(&ic);

    /* Priority ceiling against Win16Mutex inversion. On Win9x every USER/GDI
     * call takes the shared Win16Mutex; the renderer holds REALTIME while a
     * game plays and its loop enters USER too. If THIS process gets starved
     * (full-screen DOS VM) while holding that mutex during a repaint, the
     * realtime renderer blocks behind us indefinitely = system wedge. At HIGH
     * priority we still yield to the renderer (realtime > high) but outrank
     * the DOS VM, so any mutex hold of ours stays microseconds - the ceiling
     * bounds the inversion. CPU cost is nil (we run a few ms per second). */
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    ini_path();
    g_msg_reload = RegisterWindowMessage(VOPL3_MSG_RELOAD);

    /* app icon from resources: 32x32 for the window class / Alt-Tab, and the
     * real 16x16 image (not a shrunk 32) for the tray and title bar */
    g_icon    = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP));
    g_icon_sm = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 16, 16, 0);
    if (!g_icon)    g_icon    = LoadIcon(NULL, IDI_APPLICATION);
    if (!g_icon_sm) g_icon_sm = g_icon;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = g_icon;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "VOPLCFG";
    RegisterClass(&wc);

    {
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rc = { 0, 0, 404, 530 };
        AdjustWindowRect(&rc, style, FALSE);
        w = CreateWindow("VOPLCFG", APP_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT,
                         rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInst, NULL);
    }
    /* title bar gets the true 16x16 image, not a GDI-shrunk 32x32 */
    SendMessage(w, WM_SETICON, ICON_SMALL, (LPARAM)g_icon_sm);
    SendMessage(w, WM_SETICON, ICON_BIG,   (LPARAM)g_icon);

    if (start_tray) hide_to_tray();
    else { ShowWindow(w, show); UpdateWindow(w); }

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(w, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    return 0;
}
