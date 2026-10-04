/*****************************************************************************
 * VOPL3.VXD - OPL3/AdLib port-trap driver for Windows 9x
 *
 * A ring-0 static VxD. It installs VMM I/O handlers on the AdLib/OPL ports
 * 0x388-0x38B and the Sound Blaster FM ports base+8/+9 (when the renderer
 * asks, see fm_trap_install) and, for every trapped access (from a DOS box,
 * or in principle
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

/* Bits of the trap mask published as STAT[12] - mirrored in vopl3ipc.h for
 * the GUI (this file deliberately includes nothing of it). Bits 0-3 are the
 * AdLib ports 388-38B, bits 4-7 the SB FM pairs at 228/248/268/288; a set bit
 * means another VxD already owned that port, so we never saw its traffic. */
#define VOPL3_TRAP_FM_TRIED    0x0100u
#define VOPL3_TRAP_MIDI_330    0x0200u
#define VOPL3_TRAP_MIDI_331    0x0400u
#define VOPL3_TRAP_MIDI_TRIED  0x0800u

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
    DWORD ring_addr;           /* VMM-heap OPL ring (allocated at init) */
    DWORD opl_vm;              /* handle of the VM that last wrote the OPL  */
    BYTE  opl_reg[512];
    /* ---- MPU-401 (MIDI) ---- */
    DWORD mpu_uart;            /* 1 = UART mode entered                   */
    DWORD mpu_ack;             /* pending ACK byte to be read (0 = none)  */
    DWORD midi_head, midi_tail, midi_lost;
    DWORD midi_addr;           /* VMM-heap MIDI byte ring                 */
    DWORD midi_trapped;        /* 1 once 0x330/0x331 handlers installed   */
    DWORD fm_trapped;          /* 1 once 0x388-0x38B handlers installed   */
    DWORD midi_vm;             /* handle of the VM currently sending MIDI  */
    DWORD midi_vm_gone;        /* set when that VM terminates (one-shot)   */
    DWORD trap_mask;           /* ports another VxD already owned - STAT[12] */
    DWORD mpu_intel;           /* commands that exist only in intelligent mode */
    /* A trace of the MPU commands a game sends: the first 32, which show how
     * its driver sets the card up, and a rolling last 16, which show what it
     * settles into. Counters alone cannot tell "loads timed tracks" from
     * "pushes one event at a time", and there is no serial port on the
     * machines this runs on - so the control panel writes these to its log. */
    BYTE  cmd_first[32];
    DWORD cmd_first_n;
    BYTE  cmd_last[16];
    DWORD cmd_total;
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

/* queue (timestamp, reg, data) for the renderer */
static void ring_put(DWORD reg, BYTE d)
{
    if (S.ring_addr) {
        ring[ring_head & (RING_SIZE - 1)] =
              ((sys_time_ms() & 0x7FFF) << 17) | (reg << 8) | d;
        ring_head++;
    }
}

/* __stdcall so the naked trampolines can push args on the stack */
void __stdcall opl_write(DWORD port, DWORD data, DWORD vm)
{
    BYTE d = (BYTE)data;
    writes_seen++;
    S.opl_vm = vm;                        /* remember who's playing FM, so we
                                           * notice when its DOS box closes    */

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

    ring_put(opl_bank | opl_index, d);
}

DWORD __stdcall opl_read(DWORD port)
{
    reads_388++;
    /* AdLib/OPL only ever reads the status at the base (even) port */
    opl_timer_update();
    return (DWORD)opl_status;
}

/* ============ shared ring-0 plumbing for the MPU-401 engine ============
 * VPICD (interrupt virtualization) and a PIT-tick clock. Both are used by the
 * intelligent-mode engine below and by the -IrqTest build's experiment. */
#include "vpicd.h"

/* VPICD_Virtualize_IRQ: EDI = descriptor -> EAX = handle, CF set = refused */
static DWORD vpicd_virtualize(VPICD_IRQ_Descriptor *d)
{
    DWORD h = 0, cf = 0;
    void *p = d;
    _asm {
        push ebx
        push edi
        mov  edi, p
    }
    VxDCall(VPICD, Virtualize_IRQ);
    _asm {
        sbb  ebx, ebx            /* CF -> -1, no CF -> 0 (flags still fresh) */
        mov  cf, ebx
        mov  h, eax
        pop  edi
        pop  ebx
    }
    return cf ? 0 : h;
}

/* both take EAX = IRQ handle, EBX = VM handle */
static void vpicd_set_int(DWORD h, DWORD vm)
{
    _asm {
        push ebx
        mov  eax, h
        mov  ebx, vm
    }
    VxDCall(VPICD, Set_Int_Request);
    _asm pop ebx
}

static void vpicd_clear_int(DWORD h, DWORD vm)
{
    _asm {
        push ebx
        mov  eax, h
        mov  ebx, vm
    }
    VxDCall(VPICD, Clear_Int_Request);
    _asm pop ebx
}

/* EAX = IRQ handle. Acknowledges a PHYSICAL interrupt on a virtualized line. */
static void vpicd_phys_eoi(DWORD h)
{
    _asm mov eax, h
    VxDCall(VPICD, Phys_EOI);
}

/* Raw PIT ticks (1.193182 MHz), the finest clock VTD offers. The intelligent
 * engine times itself on this instead of trusting its own timer interval. */
static DWORD sys_time_pit(void)
{
    DWORD lo;
    VxDCall(VTD, VTD_Get_Real_Time);
    _asm mov lo, eax
    return lo;
}

#define MIDI_RING_SIZE 4096               /* power of two; heap-allocated */

/* One captured MIDI byte on its way to the user-mode renderer. Both the UART
 * bridge and the intelligent engine's player feed this, so everything built
 * around the ring - device choice, synth lifecycle, DOS-box release - applies
 * to both. vm = 0 keeps the current owner (the engine plays on its own). */
static void midi_ring_put(BYTE b, DWORD vm)
{
    if (!S.midi_addr) return;
    ((BYTE *)S.midi_addr)[S.midi_head & (MIDI_RING_SIZE - 1)] = b;
    S.midi_head++;
    if (vm) {
        if (vm != S.midi_vm) S.midi_vm_gone = 0;   /* a new source: an old
                                                    * gone-flag is not ours */
        S.midi_vm = vm;
    }
}

#include "mpu401i.c"      /* the intelligent-mode engine (GPL, see its head) */

/* ===================== MPU-401 (MIDI) UART emulation =====================
 * Ports 0x330 (data) / 0x331 (status+command). We emulate ONLY UART ("dumb")
 * mode as a plain byte bridge: every data byte written in UART mode is a raw
 * MIDI byte, pushed to the MIDI ring for the user-mode renderer to send via
 * midiOut. The tiny handshake (reset / enter-UART, each ACK'd with 0xFE) is
 * emulated so games detect the MPU.
 *
 * Status byte (read of 0x331):
 *   bit 0x40 (DRR) = 1 -> NOT ready to accept a write. We consume instantly,
 *                        so always 0 (ready).
 *   bit 0x80 (DSR) = 1 -> NO data available to read. 0 only while an ACK byte
 *                        is pending. (We do not do MIDI-IN.)
 */
/* __stdcall so the naked trampoline can push args on the stack */
void __stdcall mpu_write(DWORD port, DWORD data, DWORD vm)
{
    BYTE d = (BYTE)data;

    if (port & 1) {                       /* trace every command, both paths */
        if (S.cmd_first_n < sizeof(S.cmd_first))
            S.cmd_first[S.cmd_first_n++] = d;
        S.cmd_last[S.cmd_total & 15] = d;
        S.cmd_total++;
    }

    /* With intelligent mode enabled ([midi] intelligent=, passed in by the
     * renderer) the engine owns both ports; it falls back to the plain byte
     * bridge itself once a game asks for UART mode. Disabled, none of it runs
     * and this is exactly the A08 stub. */
    if (mpu_i_active()) {
        mpu_i_set_vm(vm);
        if (port & 1) {
            /* count intelligent-mode commands here too - the engine path
             * bypasses the stub below, where this used to be counted */
            if (d != 0xFF && d != 0x3F) S.mpu_intel++;
            mpu_command(d);
            S.mpu_uart = mpu_i_uart();    /* keep STAT's view in step */
            return;
        }
        if (mpu_i_uart()) midi_ring_put(d, vm);   /* UART: raw MIDI byte */
        else              mpu_data(d);
        return;
    }

    if (port & 1) {                       /* 0x331: command */
        if (d == 0xFF)      { S.mpu_uart = 0; S.mpu_ack = 0xFE; } /* reset  */
        else if (d == 0x3F) { S.mpu_uart = 1; S.mpu_ack = 0xFE; } /* ->UART */
        else { S.mpu_intel++;                 S.mpu_ack = 0xFE; } /* ack rest */
        /* mpu_intel counts the commands that are NEITHER reset nor
         * enter-UART, i.e. the intelligent-mode ones we only pretend to
         * accept. Published through STAT so the control panel can show
         * whether any game actually asks for intelligent mode - the demand
         * question behind MPU401-INTELLIGENT-PLAN.md. A game that probes and
         * then falls back to UART bumps this a little and then sends 0x3F. */
        return;
    }
    /* 0x330: data */
    if (S.mpu_uart) midi_ring_put(d, vm); /* UART mode: this is a MIDI byte */
}

DWORD __stdcall mpu_read(DWORD port)
{
    if (mpu_i_active())                   /* the engine answers both ports,
                                           * UART mode included (its queue
                                           * still holds the ACKs) */
        return (port & 1) ? mpu_i_read_status() : mpu_i_read_data();

    if (port & 1)                         /* 0x331: status */
        return S.mpu_ack ? 0x00 : 0x80;   /* ready to write; data iff ACK   */
    /* 0x330: data - hand back the pending ACK, then clear it */
    {
        DWORD b = S.mpu_ack;
        S.mpu_ack = 0;
        return b;
    }
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
        push ebx              /* arg3: VM handle (who is writing) */
        push eax              /* arg2: data */
        push edx              /* arg1: port */
        call opl_write        /* __stdcall(port, data, vm) */
        pop  edx
        pop  ecx
        pop  ebx
        ret
    }
}

/* MPU-401 trap trampoline - same convention as io_trap, calls mpu_read/write.
 * MPU access is byte-wide; non-byte ops are punted to Simulate_IO as well. */
void __declspec(naked) mpu_trap(void)
{
    _asm {
        cmp  ecx, 4
        je   _out
        test ecx, ecx
        jz   _in
        pushad
        call count_nonbyte
        popad
        int  20h
        dw   0x8094           /* 0x8000 | VMM__Simulate_IO (148) */
        dw   0x0001           /* VMM_DEVICE_ID */
    _in:
        push ebx
        push ecx
        push edx
        push edx              /* arg: port */
        call mpu_read
        pop  edx
        pop  ecx
        pop  ebx
        ret
    _out:
        push ebx
        push ecx
        push edx
        push ebx              /* arg3: VM handle (who is writing) */
        push eax              /* arg2: data */
        push edx              /* arg1: port */
        call mpu_write        /* __stdcall(port, data, vm) */
        pop  edx
        pop  ecx
        pop  ebx
        ret
    }
}

/* Install_IO_Handler that reports success (carry clear) / failure (carry set).
 * Returns 1 on success, 0 if the port is already hooked by someone else.
 * 'handler' is the naked trap trampoline to install on this port. */
static DWORD io_install(DWORD port, DWORD handler)
{
    DWORD ok = 0;
    _asm {
        push esi
        push edx
        mov  esi, handler
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
    if (installed) return;
    installed = 1;

    S.ring_addr  = heap_alloc(RING_SIZE * 4);      /* OPL register ring   */
    S.midi_addr  = heap_alloc(MIDI_RING_SIZE);     /* MPU-401 MIDI byte ring */

    /* No ports are trapped at boot. Both the FM ports (see fm_trap_install)
     * and the MPU-401 ports (330/331) are trapped ON DEMAND, when the
     * renderer asks (IOCTL_VOPL3_FM_ENABLE / IOCTL_VOPL3_MIDI_ENABLE) per the
     * choices made at install - and only where the installer steered SBEMUL
     * away from them (SoftFM=1 for FM, SBPATCH for MIDI). SBEMUL tears its
     * whole emulation down (digital audio included) if another VxD owns a
     * port it still wants, so e.g. with FM left to SBEMUL, which then keeps
     * 388 and base+8/+9, we must never grab them. */
    ser_str("\nVOPL3: Device_Init done (ports are trapped on demand)\n");
}

/* Trap the FM ports: the AdLib/OPL3 ports 388-38B, and the Sound Blaster's
 * own FM ports at base+8/+9 (228/229 with the SB at 220), where a real SB
 * answers with the same chip. Asked for only when VOPL3 plays FM, and then
 * SBEMUL (SoftFM=1) claims neither group. base+8/+9 is trapped for every SB
 * base (220/240/260/280), so the VxD needs no configured base: the pairs
 * no game uses just see no traffic. They decode like 388/389 (even port =
 * index/status, odd = data, bank 0). The rest of the SB base range (DSP,
 * mixer, and base+0..3) stays SBEMUL's - stealing any of it kills SBEMUL's
 * digital audio for DOS games. In-game: Music=AdLib (-> 388 -> here ->
 * renderer) and FX=Sound Blaster (-> SBEMUL) give OPL3 music AND digital
 * effects at the same time. */
static void fm_trap_install(void)
{
    DWORD r;
    if (S.fm_trapped) return;
    S.fm_trapped = 1;
#ifndef VOPL3_NOINSTALL   /* -DVOPL3_NOINSTALL builds a load-but-do-nothing VxD for A/B baseline tests */
    /* A port another VxD already owns is simply left to it - VMM gives a port
     * to a single owner. Which ones were refused is recorded in trap_mask and
     * published through STAT, so the control panel can say so: without that,
     * a driver that grabbed 0x388 before us (an AdLib/FM Windows driver, say)
     * leaves VOPL3 looking merely idle, with the reason only on COM1. */
    S.trap_mask |= VOPL3_TRAP_FM_TRIED;
    r = io_install(0x388, (DWORD)io_trap);  ser_str("  388 "); ser_str(r ? "OK\n" : "TAKEN\n");
    if (!r) S.trap_mask |= 1u << 0;
    r = io_install(0x389, (DWORD)io_trap);  ser_str("  389 "); ser_str(r ? "OK\n" : "TAKEN\n");
    if (!r) S.trap_mask |= 1u << 1;
    r = io_install(0x38A, (DWORD)io_trap);  ser_str("  38A "); ser_str(r ? "OK\n" : "TAKEN\n");
    if (!r) S.trap_mask |= 1u << 2;
    r = io_install(0x38B, (DWORD)io_trap);  ser_str("  38B "); ser_str(r ? "OK\n" : "TAKEN\n");
    if (!r) S.trap_mask |= 1u << 3;
    {   /* base+8/+9 for SB bases 220-280 */
        static const char *const name[4] = { "  228/229 ", "  248/249 ",
                                             "  268/269 ", "  288/289 " };
        DWORD i;
        for (i = 0; i < 4; i++) {
            r = io_install(0x228 + i * 0x20, (DWORD)io_trap);
            if (r) io_install(0x229 + i * 0x20, (DWORD)io_trap);
            else   S.trap_mask |= 1u << (4 + i);
            ser_str(name[i]); ser_str(r ? "OK\n" : "TAKEN\n");
        }
    }
#else
    (void)r; ser_str("  (NOINSTALL build: no ports trapped)\n");
#endif
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

/* Called on Destroy_VM (a DOS box closing).
 *
 * MIDI: if it's the VM that was sending MIDI, flag it so the renderer
 * releases the synth immediately - the game is gone, not just pausing.
 *
 * FM: if it's the VM that last wrote the OPL, key off every voice it left
 * sounding. A game that quits without silencing the chip otherwise leaves a
 * sustaining voice (EG-TYP set) holding at its sustain level forever: the
 * renderer never goes idle, keeps synthesizing, holds realtime under
 * priority=auto, and never reaches idleclose=. The key-offs go through the
 * ring like any game write, so the renderer applies them in order and each
 * voice fades out through its own release envelope. Block/F-number are kept
 * (only the KEY-ON bit is cleared) so the pitch doesn't jump during the
 * release. Only voices actually keyed on get a write: a box that exits with
 * the chip already silent queues nothing, and so doesn't wake a renderer
 * that has released its output. */
static void fm_vm_destroyed(DWORD vm)
{
    DWORD bank, r;
    if (!S.opl_vm || vm != S.opl_vm) return;
    S.opl_vm = 0;
    for (bank = 0; bank <= 0x100; bank += 0x100)
        for (r = bank | 0xB0; r <= (bank | 0xB8); r++)
            if (opl_reg[r] & 0x20) {                /* KEY-ON */
                opl_reg[r] &= ~0x20;
                ring_put(r, opl_reg[r]);
            }
    if (opl_reg[0xBD] & 0x1F) {                     /* rhythm BD/SD/TT/CY/HH */
        opl_reg[0xBD] &= ~0x1F;
        ring_put(0xBD, opl_reg[0xBD]);
    }
}

void __stdcall vm_destroyed(DWORD vm)
{
    if (S.midi_vm && vm == S.midi_vm) {
        S.midi_vm_gone = 1;
        S.midi_vm      = 0;
    }
    fm_vm_destroyed(vm);
}

/* ===================== Win32 DeviceIoControl bridge =====================
 * The user-mode renderer opens "\\.\VOPL3" and polls IOCTL_VOPL3_DRAIN to
 * pull queued (reg<<8)|data words out of the ring. */
#define IOCTL_VOPL3_DRAIN        0x1000 /* out: array of DWORD OPL writes    */
#define IOCTL_VOPL3_STAT         0x1001 /* out: [head,tail,lost,...]         */
#define IOCTL_VOPL3_MIDI_DRAIN   0x1002 /* out: raw MPU-401 MIDI bytes        */
#define IOCTL_VOPL3_MIDI_ENABLE  0x1003 /* start trapping 0x330/0x331 (once)  */
#define IOCTL_VOPL3_MIDI_VM_GONE 0x1004 /* out: [0] 1 if MIDI VM terminated
                                         * (1-shot); [1] 1 if the MIDI source
                                         * is a DOS box, 0 if the System VM or
                                         * unknown (only with an 8-byte buffer) */
#define IOCTL_VOPL3_FM_ENABLE    0x1005 /* start trapping the FM ports (once) */
#define IOCTL_VOPL3_MPU_INTEL    0x1006 /* in: DWORD[2] = { on, irq } - turn  */
                                        /* MPU-401 intelligent mode on, with  */
                                        /* irq 0 meaning "polled, claim no    */
                                        /* IRQ". The renderer passes what the */
                                        /* user configured; the VxD does no   */
                                        /* registry or INI reading itself.    */

static DWORD sys_vm_handle(void)
{
    DWORD h;
    _asm push ebx
    VMMCall(Get_Sys_VM_Handle);
    _asm mov h, ebx
    _asm pop ebx
    return h;
}

/* VxD revision reported by STAT out[9]: up to 4 ASCII chars, little-endian,
 * printed as a string by readers. Keep in sync with vopl3ipc.h VOPL3_REV
 * (kept literal here so the VxD's minimal build needs no extra include). */
#ifndef VOPL3_VXD_REV                   /* overridable (-D...) to tag test builds */
#define VOPL3_VXD_REV 0x00383041        /* "A08" */
#endif

DWORD __stdcall Device_IO_Control_proc(DWORD vmhandle, struct DIOCParams *params)
{
    DWORD *out = (DWORD *)params->lpOutBuffer;
    DWORD  rc  = 1;

    switch (params->dwIoControlCode) {
        case DIOC_OPEN:              /* CreateFile("\\.\VOPL3") */
        case DIOC_CLOSEHANDLE:
            rc = 0;
            break;

        case IOCTL_VOPL3_MIDI_VM_GONE:  /* renderer: did the MIDI game's box close? */
            if (params->cbOutBuffer >= 4) {
                DWORD nout = 4;
                out[0] = S.midi_vm_gone;
                S.midi_vm_gone = 0;                /* one-shot: clear on read */
                if (params->cbOutBuffer >= 8) {    /* is the source a DOS box? */
                    out[1] = (S.midi_vm && S.midi_vm != sys_vm_handle()) ? 1 : 0;
                    nout = 8;
                }
                if (params->lpcbBytesReturned)
                    *(DWORD *)params->lpcbBytesReturned = nout;
                rc = 0;
            }
            break;

        case IOCTL_VOPL3_FM_ENABLE:     /* renderer: mode includes FM */
            fm_trap_install();
            rc = 0;
            break;

        case IOCTL_VOPL3_MPU_INTEL:     /* renderer: intelligent mode + IRQ */
            if (params->cbInBuffer >= 8) {
                DWORD *in = (DWORD *)params->lpInBuffer;
                mpu_i_enable(in[0], in[1]);   /* [0] on/off, [1] IRQ (0 = polled) */
                rc = 0;
            }
            break;

        case IOCTL_VOPL3_MIDI_ENABLE:   /* renderer: mode includes MIDI */
            if (!S.midi_trapped) {
                S.trap_mask |= VOPL3_TRAP_MIDI_TRIED;
                if (!io_install(0x330, (DWORD)mpu_trap)) S.trap_mask |= VOPL3_TRAP_MIDI_330;
                if (!io_install(0x331, (DWORD)mpu_trap)) S.trap_mask |= VOPL3_TRAP_MIDI_331;
                S.midi_trapped = 1;
                ser_str("VOPL3: MPU ports 330/331 ");
                ser_str((S.trap_mask & (VOPL3_TRAP_MIDI_330 | VOPL3_TRAP_MIDI_331))
                            ? "TAKEN by another driver\n" : "OK\n");
            }
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

        case IOCTL_VOPL3_MIDI_DRAIN: {
            DWORD avail = S.midi_head - S.midi_tail;    /* unsigned wrap-safe */
            DWORD room  = params->cbOutBuffer;          /* raw bytes out      */
            BYTE *dst   = (BYTE *)params->lpOutBuffer;
            BYTE *mring = (BYTE *)S.midi_addr;
            DWORD i;
            if (!S.midi_addr) { rc = 0; break; }
            if (avail > MIDI_RING_SIZE) {               /* producer overran us */
                S.midi_lost += (avail - MIDI_RING_SIZE);
                S.midi_tail  = S.midi_head - MIDI_RING_SIZE;
                avail        = MIDI_RING_SIZE;
            }
            if (avail > room) avail = room;
            for (i = 0; i < avail; i++)
                dst[i] = mring[(S.midi_tail + i) & (MIDI_RING_SIZE - 1)];
            S.midi_tail += avail;
            if (params->lpcbBytesReturned)
                *(DWORD *)params->lpcbBytesReturned = avail;
            rc = 0;
            break;
        }

        case IOCTL_VOPL3_STAT:
            if (params->cbOutBuffer >= 12) {
                DWORD nout = 12;
                out[0] = ring_head; out[1] = ring_tail; out[2] = ring_lost;
                if (params->cbOutBuffer >= 20) {   /* extended: OPL */
                    out[3] = writes_seen;
                    out[4] = S.nonbyte;
                    nout = 20;
                }
                if (params->cbOutBuffer >= 36) {   /* extended: MPU-401/MIDI */
                    out[5] = S.midi_head;          /* total MIDI bytes captured */
                    out[6] = S.midi_tail;
                    out[7] = S.midi_lost;
                    out[8] = S.mpu_uart;           /* in UART mode? */
                    nout = 36;
                }
                if (params->cbOutBuffer >= 40) {   /* extended: VxD revision */
                    out[9] = VOPL3_VXD_REV;        /* packed major<<16|minor<<8|patch */
                    nout = 40;
                }
                if (params->cbOutBuffer >= 48) {   /* extended: port reads, ticks */
                    out[10] = reads_388;           /* OPL status-port reads */
                    out[11] = 0;                   /* reserved */
                    nout = 48;
                }
                if (params->cbOutBuffer >= 56) {   /* extended: traps, MPU mode */
                    out[12] = S.trap_mask;         /* ports another VxD owns */
                    out[13] = S.mpu_intel;         /* intelligent-mode commands */
                    nout = 56;
                }
                if (params->cbOutBuffer >= 72) {   /* extended: which MPU path
                                                    * a game actually used */
                    mpu_i_stat(&out[14]);          /* state, irqs, reqs, polls */
                    nout = 72;
                }
                if (params->cbOutBuffer >= 128) {  /* extended: command trace */
                    BYTE *o = (BYTE *)&out[18];
                    DWORD k;
                    for (k = 0; k < sizeof(S.cmd_first); k++) o[k] = S.cmd_first[k];
                    for (k = 0; k < sizeof(S.cmd_last); k++)  o[32 + k] = S.cmd_last[k];
                    out[30] = S.cmd_first_n;
                    out[31] = S.cmd_total;
                    nout = 128;
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
        cmp eax, Destroy_VM
        jnz c7
            push ebx                      /* EBX = terminating VM handle */
            call vm_destroyed
            clc
            ret
        c7:
        clc
        ret
    }
}
