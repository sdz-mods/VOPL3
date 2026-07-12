/* MIDILIST.EXE - list the MIDI OUTPUT devices Windows sees, with their index.
 * Use it on the target to find what synths are installed and which index to
 * put in VOPL3.INI [midi] device= (or 65535 for the MIDI Mapper).
 * Build: wcl386 -bt=nt -l=nt MIDILIST.C winmm.lib   (runs on Win9x..)
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
int main(void)
{
    UINT n = midiOutGetNumDevs(), i;
    MIDIOUTCAPS c;
    printf("MIDI output devices (%u):\n", n);
    printf("  65535 : (MIDI Mapper - follows Control Panel > Multimedia > MIDI)\n");
    for (i = 0; i < n; i++)
        if (midiOutGetDevCaps(i, &c, sizeof(c)) == MMSYSERR_NOERROR)
            printf("  %5u : %s\n", i, c.szPname);
    printf("\nPut one of these numbers in VOPL3.INI  [midi] device=\n");
    return 0;
}
