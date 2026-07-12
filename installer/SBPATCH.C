/* SBPATCH.EXE - patch Microsoft's SBEMUL.SYS so it stops claiming the AdLib FM
 * ports 388-38B (VOPL3 needs them), while keeping digital + MIDI.
 *
 *   1. verifies it's a PE file and its PE checksum is VALID (integrity),
 *   2. finds the FM port table by PATTERN (388,389,38A,38B as consecutive
 *      DWORDs) so it works regardless of the exact Win98 build,
 *   3. backs up the original (SBEMUL.SYS.orig),
 *   4. changes those four ports to unused 2A0-2A3,
 *   5. recomputes the PE checksum and writes it back.
 *
 * Build (Open Watcom, Win32 console - runs on Win98): see build.ps1
 * Usage: SBPATCH.EXE [path-to-SBEMUL.SYS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long  DWORD;
typedef unsigned char  BYTE;

static const BYTE FM_OFF[16] = {0x88,3,0,0, 0x89,3,0,0, 0x8A,3,0,0, 0x8B,3,0,0};
static const BYTE FM_NEW[16] = {0xA0,2,0,0, 0xA1,2,0,0, 0xA2,2,0,0, 0xA3,2,0,0};

/* Print the driver's file version by locating the VS_FIXEDFILEINFO signature
 * (0xFEEF04BD) in the version resource. FileVersion is two DWORDs after the
 * signature: MS = (major<<16)|minor, LS = (build<<16)|revision; SBEMUL uses
 * major.minor.revision (the low word of LS) - 2222 = stock 98SE, 2223 = the
 * Q269601 QFE hotfix. Robust: scans by signature, no hardcoded offset. */
static void print_version(BYTE *d, long len)
{
    long i;
    for (i = 0; i + 16 <= len; i += 4) {
        if (d[i]==0xBD && d[i+1]==0x04 && d[i+2]==0xEF && d[i+3]==0xFE) {
            unsigned minor = (unsigned)d[i+8]  | ((unsigned)d[i+9]<<8);
            unsigned major = (unsigned)d[i+10] | ((unsigned)d[i+11]<<8);
            unsigned rev   = (unsigned)d[i+12] | ((unsigned)d[i+13]<<8);
            printf("  file version: %u.%u.%u\n", major, minor, rev);
            return;
        }
    }
    printf("  file version: (version resource not found)\n");
}

/* standard PE image checksum (checksum field treated as 0), + file length */
static DWORD pe_checksum(BYTE *d, long len, long co)
{
    DWORD sum = 0; long i;
    BYTE s0=d[co], s1=d[co+1], s2=d[co+2], s3=d[co+3];
    d[co]=d[co+1]=d[co+2]=d[co+3]=0;
    for (i = 0; i+1 < len; i += 2) {
        sum += (DWORD)d[i] | ((DWORD)d[i+1] << 8);
        sum = (sum & 0xffff) + (sum >> 16);
    }
    if (len & 1) { sum += d[len-1]; sum = (sum & 0xffff) + (sum >> 16); }
    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);
    d[co]=s0; d[co+1]=s1; d[co+2]=s2; d[co+3]=s3;
    return sum + (DWORD)len;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1]
                                  : "C:\\WINDOWS\\SYSTEM32\\DRIVERS\\SBEMUL.SYS";
    FILE *f; BYTE *d; long len, pe, co, at = -1; int i, hits; DWORD stored, calc;
    char bak[300];

    printf("VOPL3 SBEMUL patcher\n  target: %s\n", path);

    f = fopen(path, "rb");
    if (!f) { printf("ERROR: cannot open file.\n"); return 1; }
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len < 0x100 || len > 0x100000) { printf("ERROR: unexpected size %ld.\n", len); fclose(f); return 1; }
    d = (BYTE *)malloc(len);
    if (!d || fread(d, 1, len, f) != (size_t)len) { printf("ERROR: read failed.\n"); fclose(f); return 1; }
    fclose(f);

    if (d[0] != 'M' || d[1] != 'Z') { printf("ERROR: not an MZ file.\n"); return 2; }
    pe = (long)d[0x3c] | ((long)d[0x3d]<<8) | ((long)d[0x3e]<<16) | ((long)d[0x3f]<<24);
    if (pe < 0 || pe + 0x60 > len || d[pe] != 'P' || d[pe+1] != 'E') { printf("ERROR: no PE header.\n"); return 2; }
    co = pe + 24 + 64;

    print_version(d, len);

    stored = (DWORD)d[co] | ((DWORD)d[co+1]<<8) | ((DWORD)d[co+2]<<16) | ((DWORD)d[co+3]<<24);
    calc   = pe_checksum(d, len, co);
    printf("  PE checksum: stored=0x%08lX computed=0x%08lX %s\n",
           stored, calc, (stored == calc) ? "(valid)" : "(MISMATCH)");

    /* already patched? */
    for (i = 0; i <= len - 16; i++)
        if (memcmp(d + i, FM_NEW, 16) == 0) {
            printf("Already patched (FM ports moved to 2A0). Nothing to do.\n");
            free(d); return 0;
        }

    if (stored != calc) {
        printf("ERROR: PE checksum is not valid - file is corrupt or was modified\n"
               "       by something else. Refusing to patch. Restore a clean\n"
               "       SBEMUL.SYS and try again.\n");
        free(d); return 3;
    }

    /* find the FM port table by pattern, must be exactly once */
    hits = 0;
    for (i = 0; i <= len - 16; i++)
        if (memcmp(d + i, FM_OFF, 16) == 0) { at = i; hits++; }
    if (hits == 0) { printf("ERROR: FM port table (388-38B) not found - unrecognised\n"
                            "       SBEMUL build. Not patching (safe).\n"); free(d); return 4; }
    if (hits > 1)  { printf("ERROR: FM port table found %d times (ambiguous). Not patching.\n", hits); free(d); return 4; }
    printf("  found FM port table at file offset 0x%lX\n", at);

    /* back up original */
    strcpy(bak, path); strcat(bak, ".orig");
    f = fopen(bak, "rb");
    if (f) { fclose(f); printf("  (backup already exists: %s)\n", bak); }
    else {
        f = fopen(bak, "wb");
        if (f) { fwrite(d, 1, len, f); fclose(f); printf("  backed up original -> %s\n", bak); }
        else   { printf("  WARNING: could not write backup file.\n"); }
    }

    /* patch + recompute checksum */
    memcpy(d + at, FM_NEW, 16);
    calc = pe_checksum(d, len, co);
    d[co] = (BYTE)calc; d[co+1] = (BYTE)(calc>>8); d[co+2] = (BYTE)(calc>>16); d[co+3] = (BYTE)(calc>>24);

    f = fopen(path, "wb");
    if (!f) { printf("ERROR: cannot write %s (is it in use? try MS-DOS mode).\n", path); free(d); return 5; }
    if (fwrite(d, 1, len, f) != (size_t)len) { printf("ERROR: write failed!\n"); fclose(f); free(d); return 5; }
    fclose(f); free(d);

    printf("SUCCESS: SBEMUL.SYS patched (388-38B -> 2A0-2A3, new checksum 0x%08lX).\n"
           "         Reboot for it to take effect.\n", calc);
    return 0;
}
