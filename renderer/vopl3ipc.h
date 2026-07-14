/* vopl3ipc.h - IPC contract shared by voplsrv.exe (the renderer) and
 * VOPLCFG.EXE (the control-panel GUI).
 *
 * Three channels, each independently optional so the GUI never hard-depends on
 * any single one being present (it degrades to "driver not loaded" / "renderer
 * stopped" rather than failing):
 *
 *   1. The VxD (\\.\VOPL3)  - IOCTL_VOPL3_STAT returns ring stats + the VxD
 *      revision. Available whenever the driver is loaded, even if voplsrv is
 *      not running. (The VxD keeps its own copy of these numbers; it does NOT
 *      include this header - its revision literal is mirrored in vopl3.c.)
 *   2. Renderer status     - a named shared-memory block the renderer publishes
 *      each service loop; the GUI opens it read-only. Absent => renderer down.
 *   3. Renderer control    - registered window messages POSTed to the renderer's
 *      hidden "VOPLSRV" window to apply INI changes live / all-notes-off.
 *
 * Included by the renderer and the GUI only. NOT by the VxD.
 */
#ifndef VOPL3IPC_H
#define VOPL3IPC_H

/* --- shared release tag (VxD, renderer and GUI ship together) --------------
 * Up to 4 ASCII chars packed little-endian into a DWORD, so readers just
 * print the bytes as a string ("A04" = Alpha 04; later "A05", "B01", "1.0").
 * The VxD mirrors the DWORD literal as VOPL3_VXD_REV in vopl3.c (its build
 * takes no extra includes); keep them in sync when bumping. */
#define VOPL3_REV       "A04"
#define VOPL3_REV_DWORD ((DWORD)'A' | ((DWORD)'0' << 8) | ((DWORD)'4' << 16))

/* --- VxD ioctl the GUI reads directly (mirror of the VxD's private define) - */
#ifndef IOCTL_VOPL3_STAT
#define IOCTL_VOPL3_STAT 0x1001    /* out: up to 40 bytes, see layout below */
#endif
/* STAT DWORD layout (bytes returned grows with the out buffer you pass):
 *   [0] ring_head  [1] ring_tail  [2] ring_lost                (>=12 bytes)
 *   [3] fm_writes  [4] fm_nonbyte                              (>=20)
 *   [5] midi_head  [6] midi_tail  [7] midi_lost  [8] mpu_uart  (>=36)
 *   [9] vxd_rev (packed, as above)                             (>=40) */

/* --- renderer status shared memory ---------------------------------------- */
#define VOPL3_STATUS_NAME  "VOPL3_STATUS"
#define VOPL3_STATUS_MAGIC 0x33504F56u   /* 'VOP3' little-endian */
#define VOPL3_STATUS_VER   1

/* --- renderer control (window messages to the "VOPLSRV" window) ------------
 * Values are obtained at runtime via RegisterWindowMessage(name) on both
 * sides, so the GUI FindWindow("VOPLSRV")s the renderer and PostMessages it. */
#define VOPL3_WNDCLASS   "VOPLSRV"
#define VOPL3_MSG_RELOAD "VOPL3_RELOAD"  /* re-read VOPL3.INI + apply live      */
#define VOPL3_MSG_PANIC  "VOPL3_PANIC"   /* midiOutReset on the synth (notes off)*/

#ifndef VOPL3_NO_STRUCT
/* Single writer (renderer), single reader (GUI); all fields are independent
 * DWORDs so a torn read at worst shows one stale value for one poll - fine for
 * a monitor. `tick` lets the GUI detect a dead renderer whose mapping lingers
 * (treat status as stale if GetTickCount()-tick is large). */
typedef struct {
    DWORD magic;        /* VOPL3_STATUS_MAGIC once initialised           */
    DWORD ver;          /* VOPL3_STATUS_VER (this struct's layout)       */
    DWORD rev;          /* renderer revision = VOPL3_VER_PACK            */
    DWORD backend;      /* 0 = Nuked OPL3, 1 = Nuked-OPL3-fast           */
    DWORD midi_on;      /* MIDI bridge enabled at this launch            */
    DWORD synth_open;   /* midiOut currently open                        */
    DWORD midi_dev;     /* current device id (0xFFFF = MIDI Mapper)      */
    DWORD realtime;     /* renderer at REALTIME_PRIORITY_CLASS right now */
    DWORD active;       /* FM producing audio (not idle-skipping)        */
    DWORD volume;       /* current FM volume percent                     */
    DWORD frames;       /* buffers rendered so far (liveness heartbeat)  */
    DWORD midi_bytes;   /* total MPU-401 bytes fed to the synth          */
    DWORD tick;         /* GetTickCount() at last update (staleness)     */
    DWORD reserved[3];  /* future fields without breaking the ABI        */
} VOPL3_STATUS;
#endif

#endif /* VOPL3IPC_H */
