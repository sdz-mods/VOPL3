/* VOPLSTAT.EXE - query VOPL3's ring stats to prove our VxD captured FM writes. */
#include <windows.h>
#include <stdio.h>
#ifndef FILE_FLAG_DELAYED_ERROR
#define FILE_FLAG_DELAYED_ERROR 0x10000000
#endif
int main(void){
    HANDLE h; FILE *f; DWORD st[4]; DWORD ret; unsigned a,b,c;
    st[0]=0; st[1]=0; st[2]=0; ret=0;
    f=fopen("C:\\VOPLSTAT.TXT","w");
    h=CreateFile("\\\\.\\VOPL3",0,0,NULL,0,FILE_FLAG_DELAYED_ERROR,NULL);
    if(h==INVALID_HANDLE_VALUE){ fputs("cannot open VOPL3 (vxd not loaded)\n",f); fclose(f); return 1; }
    DeviceIoControl(h,0x1001,NULL,0,st,12,&ret,NULL);
    a=(unsigned)st[0]; b=(unsigned)st[1]; c=(unsigned)st[2];
    fprintf(f,"VOPL3 ring head=%u tail=%u lost=%u (head>0 = captured FM)\n",a,b,c);
    fclose(f); CloseHandle(h); return 0;
}
