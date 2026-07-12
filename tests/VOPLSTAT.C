/* VOPLSTAT.EXE - query VOPL3's ring stats to prove our VxD captured FM writes.
 * head/tail advance and lost stays 0 when everything is healthy.
 *   writes  = total data-port writes trapped (byte + decomposed non-byte)
 *   nonbyte = word/dword/string I/O ops that were decomposed via Simulate_IO
 *             (>0 means the program uses e.g. word OUTs, like AdLib Tracker 2)
 *   lost    = ring overruns: register writes the renderer never saw (BAD)
 */
#include <windows.h>
#include <stdio.h>
#ifndef FILE_FLAG_DELAYED_ERROR
#define FILE_FLAG_DELAYED_ERROR 0x10000000
#endif
int main(void){
    HANDLE h; FILE *f; DWORD st[5]; DWORD ret; unsigned i;
    for(i=0;i<5;i++) st[i]=0; ret=0;
    f=fopen("C:\\VOPLSTAT.TXT","w");
    h=CreateFile("\\\\.\\VOPL3",0,0,NULL,0,FILE_FLAG_DELAYED_ERROR,NULL);
    if(h==INVALID_HANDLE_VALUE){ fputs("cannot open VOPL3 (vxd not loaded)\n",f); fclose(f); return 1; }
    DeviceIoControl(h,0x1001,NULL,0,st,20,&ret,NULL);
    fprintf(f,"VOPL3 ring head=%u tail=%u lost=%u",
            (unsigned)st[0],(unsigned)st[1],(unsigned)st[2]);
    if(ret>=20) fprintf(f," writes=%u nonbyte=%u",(unsigned)st[3],(unsigned)st[4]);
    fprintf(f,"\n(head>0 = captured FM; lost>0 = ring overrun; nonbyte>0 = word I/O in use)\n");
    printf("head=%u tail=%u lost=%u writes=%u nonbyte=%u\n",
           (unsigned)st[0],(unsigned)st[1],(unsigned)st[2],(unsigned)st[3],(unsigned)st[4]);
    fclose(f); CloseHandle(h); return 0;
}
