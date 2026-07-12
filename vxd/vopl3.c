/*****************************************************************************
 * VOPL3.VXD - OPL3/AdLib port-trap driver for Windows 9x
 *
 * A ring-0 static VxD. It installs VMM I/O handlers on the AdLib/OPL ports
 * 0x388-0x38B and, for every trapped access (from a DOS box, or in principle
 * any Win16/Win32 program that writes those ports):
 *   - latches the OPL register index and captures each (register, data) write
 *     into a ring buffer (allocated from the VMM heap);
 *   - keeps AdLib detection working (register-index latch + status/timer
 *     flags) and models the two OPL timers with their real periods, so
 *     players that pace the music off timer overflows run at correct speed.
 *
 * It does NO audio synthesis. The user-mode renderer (voplsrv.exe) drains the
 * ring buffer via DeviceIoControl and does the actual OPL3 synthesis with
 * Nuked OPL3, playing the result through waveOut. The split is deliberate:
 * only ring 0 can trap the port I/O, but the heavy FP/DSP synthesis belongs in
 * user mode.
 *
 * Optional COM1 (0x3F8) debug tracing is compiled in only with -DVOPL3_SERIAL
 * (off by default); see the serial section below.
 *
 * This file is MIT (project code). It reuses MIT-licensed VMM/VxD glue
 * (vmm.h, io32.h, code32.h) from JHRobotics' vmdisp9x.
 *****************************************************************************/

#ifndef VXD32
#error VXD32 not defined!
#endif

/* --- minimal Windows-ish types that vmm.h's DDB struct needs --- */
typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;
typedef int            BOOL;
#ifndef NULL
#define NULL 0
#endif
#define TRUE  1
#define FALSE 0

#include "vmm.h"

#define IO_IN8
#define IO_OUT8
#include "io32.h"
#include "code32.h"

/* ---- device identity ---- */
#define VOPL3_DEVICE_ID   0x4334          /* OEM/3rd-party range */
#define VOPL3_MAJOR_VER   0
#define VOPL3_MINOR_VER   1

/* VMM device id + service macros already come from vmm.h (VMM__* + VxDCall) */

/* The DDB MUST be the first item placed in the data segment or VMM cannot
 * locate it and the VxD silently fails to load. Keep it before every other
 * global. (Matches JHRobotics vmdisp9x's "must be first address" note.) */
void __declspec(naked) VXD_control(void);

DDB VXD_DDB = {
    NULL,                    /* DDB_Next            */
    DDK_VERSION,             /* DDB_SDK_Version     */
    VOPL3_DEVICE_ID,         /* DDB_Req_Device_Number */
    VOPL3_MAJOR_VER,
    VOPL3_MINOR_VER,
    0,                       /* DDB_Flags           */
    { 'V','O','P','L','3',' ',' ',' ' },   /* DDB_Name (8) */
    VDD_Init_Order - 1,      /* init just before the VDD. NB: init order does
                              * NOT affect the SBEMUL conflict — SBEMUL tears its
                              * emulation down whenever another VxD owns 388,
                              * whether we grab it before or after it inits. */
    (DWORD)VXD_control,
    0,                       /* V86 API proc  */
    0,                       /* PM API proc   */
    0, 0,                    /* API CS:IP     */
    0,                       /* Reference data */
    NULL,                    /* service table ptr  */
    0,                       /* service table size */
    NULL,                    /* Win32 service table */
    'Prev',
    sizeof(DDB),
    'Rsv1', 'Rsv2', 'Rsv3',
};

/* ==================== all mutable state (must be file-backed) =========
 * VMM does NOT reliably zero-fill this VxD's uninitialized BSS pages
 * (verified: BSS lands past the file end). So keep every mutable global in
 * ONE INITIALIZED struct: a non-zero magic forces the whole struct into
 * _DATA, which IS stored in the file and loaded correctly (all other fields
 * zero). #define aliases let the rest of the code stay unchanged. */
struct vstate {
    DWORD magic;
    DWORD ser_ready;
    DWORD installed;
    DWORD opl_index;
    DWORD opl_status;
    DWORD opl_bank;
    DWORD timer1_run, timer2_run, timer_mask;
    DWORD t1_start, t2_start;  /* Get_System_Time (ms) when timer armed */
    DWORD reads_388, writes_seen;
    DWORD nonbyte;             /* non-byte I/O ops punted to Simulate_IO */
    DWORD ring_head, ring_tail, ring_lost;
    DWORD ring_addr;           /* VMM-heap ring (allocated at init) */
    BYTE  opl_reg[512];
};
static struct vstate S = { 0x4C504F56 };   /* 'VOPL' */

#define ser_ready    S.ser_ready
#define installed    S.installed
#define opl_index    S.opl_index
#define opl_status   S.opl_status
#define opl_bank     S.opl_bank
#define timer1_run   S.timer1_run
#define timer2_run   S.timer2_run
#define timer_mask   S.timer_mask
#define reads_388    S.reads_388
#define writes_seen  S.writes_seen
#define ring_head    S.ring_head
#define ring_tail    S.ring_tail
#define ring_lost    S.ring_lost
#define ring         ((DWORD *)S.ring_addr)
#define opl_reg      S.opl_reg

/* ============================ serial log ============================
 * Debug tracing over COM1 (115200 8N1) for host capture. OFF by default;
 * build with -DVOPL3_SERIAL (build.ps1 -Serial) to enable. When disabled
 * the ser_* calls compile to no-ops, so there is zero runtime cost and the
 * VxD never touches COM1. */
#ifdef VOPL3_SERIAL
#define COM1 0x3F8

static void ser_init(void)
{
    outp(COM1 + 1, 0x00);   /* no interrupts        */
    outp(COM1 + 3, 0x80);   /* DLAB                 */
    outp(COM1 + 0, 0x01);   /* 115200 baud (div=1)  */
    outp(COM1 + 1, 0x00);
    outp(COM1 + 3, 0x03);   /* 8N1                  */
    outp(COM1 + 2, 0xC7);   /* FIFO on              */
    ser_ready = 1;
}

static void ser_ch(char c)
{
    if (!ser_ready) ser_init();
    while ((inp(COM1 + 5) & 0x20) == 0) { /* wait THR empty */ }
    outp(COM1, (BYTE)c);
}

static void ser_str(const char *s) { while (*s) ser_ch(*s++); }

static void ser_hex8(DWORD v)
{
    const char *h = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4) ser_ch(h[(v >> i) & 0xF]);
}

static void ser_dec(DWORD v)
{
    char buf[12]; int n = 0;
    if (v == 0) { ser_ch('0'); return; }
    while (v) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) ser_ch(buf[--n]);
}
#else
static void ser_str(const char *s) { (void)s; }
static void ser_dec(DWORD v)       { (void)v; }
#endif

/* ============================ OPL state ============================ */
/* OPL status/timer emulation. Timer flags used to fire IMMEDIATELY when a
 * timer was started - enough to pass AdLib detection, but wrong for any
 * software that actually measures time with the OPL timers. The periods are
 * now modelled for real (lazily: computed from elapsed time when the status
 * port is read; no polling, no interrupts): T1 ticks every 80us, T2 every
 * 320us, period = (256 - preset) * tick. Sub-2ms periods still fire on the
 * first read, because the clock below is ms-granular - which is exactly what
 * the classic detection probe (preset 0xFF = 80us period, busy-wait ~100us,
 * expect the flag) needs anyway. */

/* VTD (Virtual Timer Device) services - not in the bundled vmm.h.
 * VTD_Get_Real_Time returns raw 1.193182 MHz PIT ticks in EDX:EAX,
 * hardware-accurate regardless of how the timer interrupt is programmed
 * (VMM's Get_System_Time makes no such granularity guarantee).
 * VTD_DEVICE_ID comes from vmm.h; only the service index is added here. */
#define VTD__VTD_Get_Real_Time  7

/* true milliseconds (sub-ms accurate): low dword of PIT ticks / 1193.
 * The low dword wraps every ~60 min; the consumers only use short deltas
 * (mask 0x7FFF / timer periods), so the once-an-hour glitch re-anchors
 * harmlessly. */
static DWORD sys_time_ms(void)
{
    DWORD lo;
    VxDCall(VTD, VTD_Get_Real_Time);
    _asm mov lo, eax
    return lo / 1193;
}

/* update timer state from timer-control register (reg 4) */
static void opl_timer_ctrl(BYTE v)
{
    if (v & 0x80) {             /* IRQ reset: clear the flags. The timers keep
                                 * running; re-arm them from "now" so the next
                                 * overflow is one full period away (pacing
                                 * loops reset immediately after seeing the
                                 * flag, so the phase error is just their poll
                                 * latency). */
        opl_status = 0;
        if (timer1_run) S.t1_start = sys_time_ms();
        if (timer2_run) S.t2_start = sys_time_ms();
        return;
    }
    timer_mask = v;
    if ((v & 0x01) && !timer1_run) S.t1_start = sys_time_ms();
    timer1_run = (v & 0x01) ? 1 : 0;
    if ((v & 0x02) && !timer2_run) S.t2_start = sys_time_ms();
    timer2_run = (v & 0x02) ? 1 : 0;
}

/* evaluate timer overflows (called from the status-port read) */
static void opl_timer_update(void)
{
    if (timer1_run && !(timer_mask & 0x40) && !(opl_status & 0x40)) {
        DWORD per = ((DWORD)(256 - opl_reg[0x02]) * 80 + 500) / 1000;  /* ms */
        if (per < 2 || (sys_time_ms() - S.t1_start) >= per)
            opl_status |= 0xC0;                          /* IRQ + T1 */
    }
    if (timer2_run && !(timer_mask & 0x20) && !(opl_status & 0x20)) {
        DWORD per = ((DWORD)(256 - opl_reg[0x03]) * 320 + 500) / 1000; /* ms */
        if (per < 2 || (sys_time_ms() - S.t2_start) >= per)
            opl_status |= 0xA0;                          /* IRQ + T2 */
    }
}

/* ===================== register-write ring buffer =====================
 * Single producer (the I/O trap, ring 0) pushes one DWORD per data write:
 *
 *     [31..17] Get_System_Time & 0x7FFF   (ms timestamp, wraps at 32.7 s)
 *     [16..8]  register 0..0x1FF          (bank 1 = 0x100 | index)
 *     [7..0]   data byte
 *
 * Single consumer (the user-mode renderer, via DeviceIoControl) drains them
 * and re-applies each write at its correct sample offset. The TIMESTAMP is
 * load-bearing: without it the renderer applied a whole drain's worth of
 * writes at one instant, so a note keyed on and off within one ~10 ms drain
 * window rendered ZERO samples and vanished (measured: a 10 ms-bucketed
 * replay deletes a sizeable fraction of 6 ms staccato notes outright).
 * The renderer only uses timestamp DELTAS between consecutive entries, so
 * the 15-bit wrap is harmless.
 * No per-write serial logging here: the serial busy-wait would destroy
 * real-time timing. */
#define RING_SIZE 4096                    /* power of two; heap-allocated. Sized
                                           * for tracker-style write bursts
                                           * (whole instrument banks per tick)
                                           * between renderer drains. */

/* __stdcall so the naked trampolines can push args on the stack */
void __stdcall opl_write(DWORD port, DWORD data)
{
    BYTE d = (BYTE)data;
    writes_seen++;

    if ((port & 1) == 0) {
        /* address/index port: 0x388 = bank 0 (OPL2/OPL3 set A),
         *                     0x38A = bank 1 (OPL3 set B)          */
        opl_index = d;
        opl_bank  = (port == 0x38A) ? 0x100 : 0x000;
        return;
    }

    /* data port: keep local shadow + timer/status emulation for detection */
    if (opl_bank == 0 && opl_index == 0x04) {
        opl_timer_ctrl(d);
    } else if (opl_bank == 0 && opl_index == 0x02) {
        opl_reg[0x02] = d;                       /* timer1 preset */
    } else if (opl_bank == 0 && opl_index == 0x03) {
        opl_reg[0x03] = d;                       /* timer2 preset */
    } else {
        opl_reg[opl_bank | opl_index] = d;
    }

    /* queue (timestamp, reg, data) for the renderer */
    if (S.ring_addr) {
        ring[ring_head & (RING_SIZE - 1)] =
              ((sys_time_ms() & 0x7FFF) << 17)
            | ((DWORD)(opl_bank | opl_index) << 8) | d;
        ring_head++;
    }
}

DWORD __stdcall opl_read(DWORD port)
{
    reads_388++;
    /* AdLib/OPL only ever reads the status at the base (even) port */
    opl_timer_update();
    return (DWORD)opl_status;
}

/* ===================== I/O trap trampolines =====================
 * VMM Install_IO_Handler callback convention:
 *   EAX = data (for OUT), EBX = VM handle, ECX = I/O type,
 *   EDX = port, EBP -> client regs. For byte IN, return value in AL.
 * ECX encodes direction and width: 0 = byte input, 4 = byte output;
 * anything else (word/dword/string/REP forms, with their flag bits) is
 * JUMPED to VMM's Simulate_IO, which decomposes the operation into byte
 * accesses and calls this handler again for each one. Word/dword access
 * to the OPL ports is legal on real hardware (the ISA bus splits e.g. a
 * word OUT to 388 into byte cycles at 388 and 389), so a byte-only
 * handler would silently drop the extra bytes for software that uses it.
 * The 'nonbyte' counter (STAT ioctl / VOPLSTAT) shows whether any such
 * I/O is actually occurring. */
static void __stdcall count_nonbyte(void) { S.nonbyte++; }

void __declspec(naked) io_trap(void)
{
    _asm {
        cmp  ecx, 4
        je   _out
        test ecx, ecx
        jz   _in
        /* ---- not a plain byte op: let VMM decompose it ----
         * VMMJmp(Simulate_IO) emitted by hand (0x8000+148, VMM=1): the
         * C macro would start new _asm blocks and Watcom scopes asm
         * labels per block, breaking the jumps above. */
        pushad
        call count_nonbyte
        popad
        int  20h
        dw   0x8094           /* 0x8000 | VMM__Simulate_IO (148) */
        dw   0x0001           /* VMM_DEVICE_ID */
    _in:
        /* ---- byte input ---- */
        push ebx
        push ecx
        push edx
        push edx              /* arg: port */
        call opl_read         /* __stdcall, returns byte in EAX */
        pop  edx
        pop  ecx
        pop  ebx
        ret                   /* EAX = status byte */
    _out:
        push ebx
        push ecx
        push edx
        push eax              /* arg2: data */
        push edx              /* arg1: port */
        call opl_write        /* __stdcall(port, data) */
        pop  edx
        pop  ecx
        pop  ebx
        ret
    }
}

/* Install_IO_Handler that reports success (carry clear) / failure (carry set).
 * Returns 1 on success, 0 if the port is already hooked by someone else. */
static DWORD io_install(DWORD port)
{
    DWORD ok = 0;
    _asm {
        push esi
        push edx
        mov  esi, offset io_trap
        mov  edx, port
    }
    VMMCall(Install_IO_Handler);
    _asm {
        jc   _failed
        mov  ok, 1
    _failed:
        pop  edx
        pop  esi
    }
    return ok;
}

/* Allocate zeroed system memory from the VMM heap. Large buffers must NOT
 * live in the VxD's static data (it is not fully mapped at runtime); the heap
 * gives a valid ring-0 flat pointer. Returns 0 on failure. */
#define HEAPZEROINIT 0x00000001
static DWORD heap_alloc(DWORD nbytes)
{
    DWORD p = 0;
    _asm {
        push HEAPZEROINIT
        push nbytes
    }
    VMMCall(_HeapAllocate);
    _asm {
        add  esp, 8
        mov  p, eax
    }
    return p;
}

/* ============================ init ============================ */
static void do_install(void)
{
    DWORD r;
    if (installed) return;
    installed = 1;

    S.ring_addr = heap_alloc(RING_SIZE * 4);   /* the register-write ring */

    ser_str("\nVOPL3: Device_Init, installing I/O handlers\n");

    /* Trap ONLY the dedicated AdLib/OPL ports 388-38B. We must not trap the
     * SoundBlaster base range (220/221/228/229): those belong to SBEMUL's SB
     * emulation (DSP + mixer + SB-native FM), and stealing them kills SBEMUL's
     * digital audio for DOS games. 388-38B is the standard AdLib/OPL3 window,
     * outside the SB base, so SBEMUL keeps working. In-game: set Music=AdLib
     * (-> 388 -> here -> renderer) and FX=Sound Blaster (-> SBEMUL) to get
     * OPL3 music AND digital effects at the same time. */
#ifndef VOPL3_NOINSTALL   /* -DVOPL3_NOINSTALL builds a load-but-do-nothing VxD for A/B baseline tests */
    r = io_install(0x388); ser_str("  388 "); ser_str(r ? "OK\n"   : "TAKEN\n");
    r = io_install(0x389); ser_str("  389 "); ser_str(r ? "OK\n"   : "TAKEN\n");
    r = io_install(0x38A); ser_str("  38A "); ser_str(r ? "OK\n"   : "TAKEN\n");
    r = io_install(0x38B); ser_str("  38B "); ser_str(r ? "OK\n"   : "TAKEN\n");
#else
    (void)r; ser_str("  (NOINSTALL build: no ports trapped)\n");
#endif

    ser_str("VOPL3: install done\n");
}

void __stdcall ctrl_log(DWORD msg)
{
#ifdef VOPL3_SERIAL
    if (msg == W32_DEVICEIOCONTROL) return;   /* don't spam per ioctl poll */
    ser_str("CTRL ");
    ser_hex8(msg);
    ser_ch('\n');
#else
    (void)msg;
#endif
}

void __stdcall Device_Init_proc(DWORD VM)     { do_install(); }
void __stdcall Device_Exit_proc(DWORD VM)
{
    ser_str("VOPL3: exit. reads(388)=");
    ser_dec(reads_388);
    ser_str(" writes=");
    ser_dec(writes_seen);
    ser_str("\n");
}

/* ===================== Win32 DeviceIoControl bridge =====================
 * The user-mode renderer opens "\\.\VOPL3" and polls IOCTL_VOPL3_DRAIN to
 * pull queued (reg<<8)|data words out of the ring. */
#define IOCTL_VOPL3_DRAIN 0x1000    /* out: array of DWORD writes  */
#define IOCTL_VOPL3_STAT  0x1001    /* out: [head, tail, lost]     */

DWORD __stdcall Device_IO_Control_proc(DWORD vmhandle, struct DIOCParams *params)
{
    DWORD *out = (DWORD *)params->lpOutBuffer;
    DWORD  rc  = 1;

    switch (params->dwIoControlCode) {
        case DIOC_OPEN:              /* CreateFile("\\.\VOPL3") */
        case DIOC_CLOSEHANDLE:
            rc = 0;
            break;

        case IOCTL_VOPL3_DRAIN: {
            DWORD avail = ring_head - ring_tail;   /* unsigned wrap-safe */
            DWORD room  = params->cbOutBuffer >> 2;
            DWORD i;
            if (!S.ring_addr) { rc = 0; break; }
            if (avail > RING_SIZE) {               /* producer overran us */
                ring_lost += (avail - RING_SIZE);
                ring_tail  = ring_head - RING_SIZE;
                avail      = RING_SIZE;
            }
            if (avail > room) avail = room;
            for (i = 0; i < avail; i++)
                out[i] = ring[(ring_tail + i) & (RING_SIZE - 1)];
            ring_tail += avail;
            if (params->lpcbBytesReturned)
                *(DWORD *)params->lpcbBytesReturned = avail << 2;
            rc = 0;
            break;
        }

        case IOCTL_VOPL3_STAT:
            if (params->cbOutBuffer >= 12) {
                DWORD nout = 12;
                out[0] = ring_head; out[1] = ring_tail; out[2] = ring_lost;
                if (params->cbOutBuffer >= 20) {   /* extended stats */
                    out[3] = writes_seen;
                    out[4] = S.nonbyte;
                    nout = 20;
                }
                if (params->lpcbBytesReturned)
                    *(DWORD *)params->lpcbBytesReturned = nout;
                rc = 0;
            }
            break;
    }
    return rc;
}

void __declspec(naked) Device_IO_Control_entry(void)
{
    _asm {
        push esi          /* struct DIOCParams * */
        push ebx          /* VM handle           */
        call Device_IO_Control_proc
        retn
    }
}

/* ===================== VXD control dispatch ===================== */
void __declspec(naked) VXD_control(void)
{
    _asm {
        pushad
        push eax
        call ctrl_log
        popad

        cmp eax, Sys_Critical_Init
        jnz c1
            clc
            ret
        c1:
        cmp eax, Device_Init
        jnz c2
            push ebx
            call Device_Init_proc
            clc
            ret
        c2:
        cmp eax, Sys_Dynamic_Device_Init
        jnz c3
            push ebx
            call Device_Init_proc
            clc
            ret
        c3:
        cmp eax, System_Exit
        jnz c4
            push ebx
            call Device_Exit_proc
            clc
            ret
        c4:
        cmp eax, Sys_Dynamic_Device_Exit
        jnz c5
            push ebx
            call Device_Exit_proc
            clc
            ret
        c5:
        cmp eax, W32_DEVICEIOCONTROL
        jnz c6
            jmp Device_IO_Control_entry
        c6:
        clc
        ret
    }
}
