/* SBPATCH.EXE - patch Microsoft's SBEMUL.SYS so it stops claiming the ports
 * VOPL3 takes over in the chosen mode, while keeping its digital audio:
 *   FM only    - AdLib FM ports 388-38B
 *   FM + MIDI  - 388-38B and the MPU-401 MIDI ports 330/331
 *   MIDI only  - 330/331 only; SBEMUL keeps 388-38B (stock AdLib behaviour)
 *
 *   1. verifies it's a PE file; a stale PE checksum only WARNS (third-party
 *      patches - e.g. the SB16-enable patch - skip the fixup, and Win9x
 *      loads such files anyway; the pattern match below is the hard gate),
 *   2. finds each port table by PATTERN (e.g. 388,389,38A,38B as consecutive
 *      DWORDs, required exactly once) so it works regardless of the exact
 *      Win98 build and refuses anything unrecognisable,
 *   3. backs up the original (SBEMUL.SYS.orig),
 *   4. sets each table to what the mode needs: moved to unused ports
 *      (2A0-2A3 / 2A4-2A5), or given back to SBEMUL if an earlier install in
 *      another mode had moved it,
 *   5. recomputes the PE checksum and writes it back (also repairing a
 *      previously stale one).
 *
 * Build (Open Watcom, Win32 console - runs on Win98): see build.ps1
 * Usage: SBPATCH.EXE [path-to-SBEMUL.SYS] [fm | fmmidi | midionly]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long  DWORD;
typedef unsigned char  BYTE;

static const BYTE FM_OFF[16] = {0x88,3,0,0, 0x89,3,0,0, 0x8A,3,0,0, 0x8B,3,0,0};
static const BYTE FM_NEW[16] = {0xA0,2,0,0, 0xA1,2,0,0, 0xA2,2,0,0, 0xA3,2,0,0};
/* MPU-401 MIDI ports 330/331 -> unused 2A4/2A5, so VOPL3 owns the MIDI ports
 * and its routable MIDI bridge replaces SBEMUL's fixed kernel GS synth. */
static const BYTE MI_OFF[8]  = {0x30,3,0,0, 0x31,3,0,0};
static const BYTE MI_NEW[8]  = {0xA4,2,0,0, 0xA5,2,0,0};

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

/* find a byte pattern; returns file offset (or -1) and sets *hits to the count */
static long find_once(BYTE *d, long len, const BYTE *pat, int patlen, int *hits)
{
    long i, at = -1; *hits = 0;
    for (i = 0; i <= len - patlen; i++)
        if (memcmp(d + i, pat, patlen) == 0) { at = i; (*hits)++; }
    return at;
}

int main(int argc, char **argv)
{
    static const char *mode_name[3] = { "FM only", "FM + MIDI", "MIDI only" };
    const char *path = NULL;
    int   mode = 0;                       /* 0 = FM only, 1 = FM + MIDI, 2 = MIDI only */
    FILE *f; BYTE *d; long len, pe, co, fm_at, fm_back, mi_at, mi_back;
    int   i, fm_off_n, fm_new_n, mi_off_n, mi_new_n;
    int   want_fm_moved, want_mi_moved, fm_act = 0, mi_act = 0;  /* +1 move, -1 restore */
    DWORD stored, calc;
    char bak[300];

    /* args: [path-to-SBEMUL.SYS] [fm | fmmidi | midionly]
     * ("midi" is accepted for fmmidi - what older installers passed) */
    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "fm") == 0)       mode = 0;
        else if (strcmp(argv[i], "fmmidi") == 0 ||
                 strcmp(argv[i], "midi") == 0)     mode = 1;
        else if (strcmp(argv[i], "midionly") == 0) mode = 2;
        else if (!path) path = argv[i];
    }
    if (!path) path = "C:\\WINDOWS\\SYSTEM32\\DRIVERS\\SBEMUL.SYS";
    want_fm_moved = (mode != 2);
    want_mi_moved = (mode != 0);

    printf("VOPL3 SBEMUL patcher\n  target: %s\n  mode: %s\n",
           path, mode_name[mode]);

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

    /* Current state of each table: where the original and the relocated
     * pattern occur. Each table is then set to what the mode needs - moved
     * or original - so re-running with a different mode (e.g. reinstalling
     * FM + MIDI as FM only, or anything as MIDI only) also moves ports BACK
     * to SBEMUL. Every write needs its pattern exactly once; anything
     * ambiguous is refused with nothing written. */
    fm_at   = find_once(d, len, FM_OFF, 16, &fm_off_n);
    fm_back = find_once(d, len, FM_NEW, 16, &fm_new_n);
    mi_at   = find_once(d, len, MI_OFF, 8,  &mi_off_n);
    mi_back = find_once(d, len, MI_NEW, 8,  &mi_new_n);

    /* FM ports 388-38B: moved to 2A0-2A3 for the FM modes (required - an
     * FM mode is pointless without it), left to SBEMUL for MIDI only. */
    if (want_fm_moved && fm_new_n == 0) {
        if (fm_off_n == 0) { printf("ERROR: FM port table (388-38B) not found - unrecognised\n"
                                    "       SBEMUL build. Not patching (safe).\n"); free(d); return 4; }
        if (fm_off_n > 1)  { printf("ERROR: FM port table found %d times (ambiguous). Not patching.\n", fm_off_n); free(d); return 4; }
        fm_act = 1;
    } else if (!want_fm_moved && fm_new_n > 0) {
        if (fm_new_n > 1)  { printf("ERROR: relocated FM port table found %d times (ambiguous) -\n"
                                    "       cannot give 388-38B back to SBEMUL. Not patching.\n", fm_new_n); free(d); return 4; }
        fm_act = -1;
    }

    /* MPU-401 MIDI ports 330/331: moved to 2A4/2A5 for the MIDI modes, left
     * to SBEMUL for FM only. In FM + MIDI a missing table is best-effort (FM
     * still gets patched); in MIDI only it is the whole point, so an error. */
    if (want_mi_moved && mi_new_n == 0) {
        if (mi_off_n == 1) mi_act = 1;
        else if (mode == 2) {
            printf("ERROR: MIDI port table (330/331) %s. Not patching.\n",
                   mi_off_n ? "found more than once (ambiguous)" : "not found - unrecognised SBEMUL build");
            free(d); return 4;
        }
        else if (mi_off_n == 0) printf("  NOTE: MIDI port table (330/331) not found - SBEMUL MIDI left as-is.\n");
        else                    printf("  NOTE: MIDI port table found %d times (ambiguous) - left as-is.\n", mi_off_n);
    } else if (!want_mi_moved && mi_new_n > 0) {
        if (mi_new_n > 1)  { printf("ERROR: relocated MIDI port table found %d times (ambiguous) -\n"
                                    "       cannot give 330/331 back to SBEMUL. Not patching.\n", mi_new_n); free(d); return 4; }
        mi_act = -1;
    }

    if (!fm_act && !mi_act) {
        printf("Already patched for this mode (FM %s, MIDI %s). Nothing to do.\n",
               fm_new_n ? "moved" : "SBEMUL's", mi_new_n ? "moved" : "SBEMUL's");
        free(d); return 0;
    }

    /* A pristine or patched-by-us file has a valid checksum. A mismatch
     * usually means a THIRD-PARTY patch that skipped the checksum fixup
     * (seen in the wild: the SB16-enable patch) - Win9x loads such files
     * fine, so warn and continue. The exactly-once port-table pattern
     * matches below are the real safety gate, and our rewrite installs a
     * correct checksum either way. */
    if (stored != calc)
        printf("WARNING: PE checksum is stale - the file was already modified by\n"
               "         another patch (e.g. SB16 enable), or is damaged. Continuing;\n"
               "         the port tables must still match exactly, and the patched\n"
               "         file gets a correct checksum.\n");

    /* back up the original before the FIRST modification - i.e. only while
     * the file still has none of our relocations (a backup taken of an
     * already-patched file would not be the original) */
    if (fm_new_n == 0 && mi_new_n == 0) {
        strcpy(bak, path); strcat(bak, ".orig");
        f = fopen(bak, "rb");
        if (f) { fclose(f); printf("  (backup already exists: %s)\n", bak); }
        else {
            f = fopen(bak, "wb");
            if (f) { fwrite(d, 1, len, f); fclose(f); printf("  backed up original -> %s\n", bak); }
            else   { printf("  WARNING: could not write backup file.\n"); }
        }
    }

    if (fm_act > 0) { memcpy(d + fm_at, FM_NEW, 16);
                      printf("  FM   table at 0x%lX -> 388-38B moved to 2A0-2A3\n", fm_at); }
    if (fm_act < 0) { memcpy(d + fm_back, FM_OFF, 16);
                      printf("  FM   table at 0x%lX -> 388-38B given back to SBEMUL\n", fm_back); }
    if (mi_act > 0) { memcpy(d + mi_at, MI_NEW, 8);
                      printf("  MIDI table at 0x%lX -> 330/331 moved to 2A4/2A5\n", mi_at); }
    if (mi_act < 0) { memcpy(d + mi_back, MI_OFF, 8);
                      printf("  MIDI table at 0x%lX -> 330/331 given back to SBEMUL\n", mi_back); }

    calc = pe_checksum(d, len, co);
    d[co] = (BYTE)calc; d[co+1] = (BYTE)(calc>>8); d[co+2] = (BYTE)(calc>>16); d[co+3] = (BYTE)(calc>>24);

    f = fopen(path, "wb");
    if (!f) { printf("ERROR: cannot write %s (is it in use? try MS-DOS mode).\n", path); free(d); return 5; }
    if (fwrite(d, 1, len, f) != (size_t)len) { printf("ERROR: write failed!\n"); fclose(f); free(d); return 5; }
    fclose(f); free(d);

    printf("SUCCESS: SBEMUL.SYS patched (new checksum 0x%08lX). Reboot to apply.\n", calc);
    return 0;
}
