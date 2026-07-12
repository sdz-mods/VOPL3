/* VOPLSTAT.EXE - query VOPL3's ring stats to prove the VxD captured FM and
 * MIDI traffic. Writes C:\VOPLSTAT.TXT and prints the same line.
 *   FM:   head/tail advance, lost stays 0 when healthy
 *     writes  = total OPL data-port writes trapped (byte + decomposed nonbyte)
 *     nonbyte = word/dword/string I/O ops decomposed via Simulate_IO
 *     lost    = OPL ring overruns (register writes the renderer never saw: BAD)
 *   MIDI (MPU-401):
 *     mhead   = total MIDI bytes captured from port 0x330 (>0 = MIDI seen)
 *     mlost   = MIDI ring overruns (BAD)
 *     uart    = 1 once the program entered MPU-401 UART mode
 */
#include <windows.h>
#include <stdio.h>
#ifndef FILE_FLAG_DELAYED_ERROR
#define FILE_FLAG_DELAYED_ERROR 0x10000000
#endif
int main(void){
    HANDLE h; FILE *f; DWORD st[9]; DWORD ret; unsigned i;
    for(i=0;i<9;i++) st[i]=0; ret=0;
    f=fopen("C:\\VOPLSTAT.TXT","w");
    h=CreateFile("\\\\.\\VOPL3",0,0,NULL,0,FILE_FLAG_DELAYED_ERROR,NULL);
    if(h==INVALID_HANDLE_VALUE){ fputs("cannot open VOPL3 (vxd not loaded)\n",f); fclose(f); return 1; }
    DeviceIoControl(h,0x1001,NULL,0,st,36,&ret,NULL);
    fprintf(f,"VOPL3 FM: head=%u tail=%u lost=%u",
            (unsigned)st[0],(unsigned)st[1],(unsigned)st[2]);
    if(ret>=20) fprintf(f," writes=%u nonbyte=%u",(unsigned)st[3],(unsigned)st[4]);
    if(ret>=36) fprintf(f,"\nVOPL3 MIDI: mhead=%u mtail=%u mlost=%u uart=%u",
            (unsigned)st[5],(unsigned)st[6],(unsigned)st[7],(unsigned)st[8]);
    fprintf(f,"\n(FM head>0 = captured FM; MIDI mhead>0 = captured MIDI; lost>0 = overrun)\n");
    printf("FM head=%u lost=%u writes=%u nonbyte=%u | MIDI mhead=%u mlost=%u uart=%u\n",
           (unsigned)st[0],(unsigned)st[2],(unsigned)st[3],(unsigned)st[4],
           (unsigned)st[5],(unsigned)st[7],(unsigned)st[8]);
    fclose(f); CloseHandle(h); return 0;
}
