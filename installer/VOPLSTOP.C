/* VOPLSTOP.EXE - gracefully stop a running VOPLSRV renderer before (re)install.
 *
 * The renderer owns a hidden top-level window of class "VOPLSRV" that handles
 * WM_CLOSE by running its clean shutdown (waveOutReset/close, midiOutReset/
 * close) and exiting. We post WM_CLOSE to it and wait for the window (hence
 * the process) to disappear, so its .EXE is no longer locked and INSTALL.BAT
 * can overwrite it. No-op (exit 0) if the renderer isn't running.
 *
 * Console app so INSTALL.BAT waits for it. Build: wcl386 -bt=nt -l=nt.
 */
#include <windows.h>
int main(void)
{
    HWND h = FindWindow("VOPLSRV", NULL);
    int  i;
    if (!h) return 0;                       /* not running */
    PostMessage(h, WM_CLOSE, 0, 0);
    for (i = 0; i < 50 && FindWindow("VOPLSRV", NULL); i++)
        Sleep(100);                         /* wait up to ~5 s for it to close */
    Sleep(400);                             /* let the process fully release the exe */
    return 0;
}
