/* VOPLSTOP.EXE - gracefully stop running VOPL3 programs before (re)install.
 *
 * The renderer owns a hidden top-level window of class "VOPLSRV" that handles
 * WM_CLOSE by running its clean shutdown (waveOutReset/close, midiOutReset/
 * close) and exiting; the control panel's "VOPLCFG" window exits on WM_CLOSE
 * too. We post WM_CLOSE to each and wait for the window (hence the process)
 * to disappear, so their .EXEs are no longer locked and INSTALL.BAT can
 * overwrite them. No-op (exit 0) for whichever isn't running.
 *
 * Console app so INSTALL.BAT waits for it. Build: wcl386 -bt=nt -l=nt.
 */
#include <windows.h>

static int stop_class(const char *cls)
{
    HWND h = FindWindow(cls, NULL);
    int  i;
    if (!h) return 0;                       /* not running */
    PostMessage(h, WM_CLOSE, 0, 0);
    for (i = 0; i < 50 && FindWindow(cls, NULL); i++)
        Sleep(100);                         /* wait up to ~5 s for it to close */
    return 1;
}

int main(void)
{
    int any = 0;
    any |= stop_class("VOPLSRV");           /* renderer      */
    any |= stop_class("VOPLCFG");           /* control panel */
    if (any) Sleep(400);          /* let the processes fully release the exes */
    return 0;
}
