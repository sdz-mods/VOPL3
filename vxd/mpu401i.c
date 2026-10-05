/*****************************************************************************
 * mpu401i.c - MPU-401 INTELLIGENT MODE engine for VOPL3.VXD
 *
 * Copyright (C) 2002-2012  The DOSBox Team
 * Copyright (C) 2013-2014  bjt, elianda          (SoftMPU)
 * Copyright (C) 2026       VOPL3
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * ---------------------------------------------------------------------------
 * The structure of this engine - the state layout, the command dispatch, the
 * host queue, the track buffers and the request-mask handling - follows
 * DOSBox's mpu401.cpp and bjt42's SoftMPU, the two implementations with real
 * mileage behind them. VOPL3's own work is the Win9x half: the VMM/VPICD
 * plumbing, the drift-compensated clock, and the hand-off of played MIDI to
 * the user-mode renderer.
 *
 * BECAUSE OF THIS FILE, VOPL3.VXD AS A WHOLE IS GPL. The user-mode components
 * (renderer, control panel, installer) are separate programs that talk to the
 * driver through an ioctl and a shared-memory block - they do not link against
 * it and stay under VOPL3's own licence. vxd/vopl3.c deliberately includes no
 * header of theirs; keep that boundary.
 *
 * ---------------------------------------------------------------------------
 * WHAT INTELLIGENT MODE IS, in one paragraph: the original MPU-401 carried a
 * Z80 running a small sequencer. The game loads timed events into eight track
 * buffers plus a conductor track, sets a timebase and tempo, and starts play;
 * the CARD then keeps time, emits the events, and interrupts the host when it
 * wants more data (0xF0+track) or when its clock-to-host interval elapses
 * (0xFD). Everything the card sends the host goes through one 32-byte queue,
 * and the IRQ is asserted exactly while that queue is non-empty.
 *
 * Included by vopl3.c as part of one translation unit (the VxD builds from a
 * single .c, and all mutable state must live in an INITIALISED struct - VMM
 * does not reliably zero this driver's BSS, see vopl3.c).
 *****************************************************************************/

#define MPU_VERSION    0x15
#define MPU_REVISION   0x01
#define MPU_QUEUE      32
#define MPU_TRACKS     8

/* what the card sends up to the host */
#define MSG_DATA_REQ   0xF0        /* +track: send me more for that track */
#define MSG_OVERFLOW   0xF8
#define MSG_COMMAND_REQ 0xF9
#define MSG_END        0xFC
#define MSG_CLOCK      0xFD
#define MSG_ACK        0xFE

/* event types held in a track buffer */
#define T_OVERFLOW     0
#define T_MARK         1
#define T_MIDI_SYS     2
#define T_MIDI_NORM    3
#define T_COMMAND      4

/* The MPU's clock runs at tempo (quarter notes/min) * timebase (ticks per
 * quarter) ticks per minute. In PIT ticks (1193182 Hz) one MPU tick is
 * therefore 1193182*60 / (tempo*timebase) - integer, no FP in ring 0. */
#define MPU_PIT_PER_MIN 71590920UL

typedef struct {
    long  counter;                 /* PIT-less: counts down in MPU ticks */
    BYTE  value[8];
    BYTE  vlength;
    BYTE  type;
} mpu_track;

typedef struct {
    DWORD magic;                   /* forces the struct into _DATA, see vopl3.c */
    DWORD enabled;                 /* intelligent mode allowed at all (ioctl) */
    DWORD irq;                     /* IRQ the game is configured for */
    DWORD irq_handle;              /* VPICD handle, 0 = not virtualized */
    DWORD irq_pending;             /* we asserted it, guest has not EOI'd */
    DWORD irq_busy;                /* the configured IRQ was already in use */
    DWORD uart;                    /* 1 = UART mode (the A08 behaviour) */
    DWORD vm;                      /* VM that owns this MPU session */

    BYTE  queue[MPU_QUEUE];        /* bytes waiting for the host to read */
    DWORD queue_pos, queue_used;

    mpu_track playbuf[MPU_TRACKS];
    mpu_track condbuf;             /* the conductor track */

    DWORD playing, conductor, cond_req, cond_set;
    DWORD wsd, wsm, wsd_start;     /* write-send-data / write-system-message */
    DWORD wsd_len;                 /* data bytes still owed by this message  */
    BYTE  wsd_status;              /* its status byte, for running status    */
    DWORD track;                   /* track the host is currently feeding */
    DWORD tmask, cmask, amask;     /* track / counter / active masks */
    DWORD midi_mask;               /* which MIDI channels we own */
    DWORD req_mask;                /* tracks (and bit 13: clock) wanting data */
    DWORD command_byte;            /* a command still waiting for its data */

    DWORD tempo, timebase, tempo_rel;
    DWORD cth_rate, cth_counter, clock_to_host;

    VPICD_IRQ_Descriptor desc;     /* VPICD keeps this pointer - and it has to
                                    * live in this INITIALISED struct: the VxD's
                                    * BSS is not file-backed (see vopl3.c) */
    DWORD last_pit;                /* PIT time the clock was last advanced to */
    DWORD timer_armed;
    DWORD ticks;                   /* MPU ticks processed (diagnostics) */
    /* What a game actually did with us - published through STAT so the
     * control panel can say which path a title really took. "It made sound"
     * does not distinguish UART from intelligent, or interrupts from polling,
     * because all three are served at once. */
    DWORD n_irq;                   /* interrupts raised into the DOS box */
    DWORD n_hw;                    /* PHYSICAL interrupts seen on our line */
    DWORD n_req;                   /* data requests sent to the host     */
    DWORD n_poll;                  /* status-port reads while data waited */
    DWORD saw_uart;                /* a game asked for UART mode (0x3F)  */
} mpu_state;

static mpu_state mpu = { 0x3130504DUL };   /* 'MP01' - must be non-zero */

static void mpu_irq_raise(void);
static void mpu_irq_clear(void);
static void mpu_timer_arm(void);
static void mpu_eoi_handler(void);

/* ------------------------------------------------------------------ helpers */

/* Hand a played MIDI byte to the user-mode renderer, through the same ring
 * the UART path uses - so intelligent mode inherits the device selection,
 * the synth lifecycle and everything else already built around it. */
static void mpu_play_byte(BYTE b)
{
    midi_ring_put(b, mpu.vm);
}

static void mpu_play_bytes(const BYTE *p, DWORD n)
{
    while (n--) mpu_play_byte(*p++);
}

/* ---------------------------------------------------------------- the queue
 * Everything the card says to the host goes through here, and the IRQ is
 * asserted exactly while the queue is non-empty. Overflow is reported the way
 * the hardware does it: the queue is cleared and a single overflow message is
 * left in it. */
static void mpu_q(BYTE b)
{
    DWORD pos;
    if (mpu.queue_used >= MPU_QUEUE) {     /* no room: report an overflow */
        mpu.queue_used = 0;
        mpu.queue_pos  = 0;
        mpu.queue[0]   = MSG_OVERFLOW;
        mpu.queue_used = 1;
        return;
    }
    pos = (mpu.queue_pos + mpu.queue_used) % MPU_QUEUE;
    mpu.queue[pos] = b;
    if (!mpu.queue_used) {                 /* empty -> non-empty: interrupt */
        mpu.queue_used = 1;
        mpu_irq_raise();
        return;
    }
    mpu.queue_used++;
}

static BYTE mpu_dequeue(void)
{
    BYTE b;
    if (!mpu.queue_used) return MSG_ACK;   /* nothing there; harmless */
    b = mpu.queue[mpu.queue_pos];
    mpu.queue_pos = (mpu.queue_pos + 1) % MPU_QUEUE;
    mpu.queue_used--;
    if (!mpu.queue_used) mpu_irq_clear();  /* drained: drop the line */
    return b;
}

/* ------------------------------------------------------------- track output
 * A track's events are (counter, MIDI bytes). When its counter runs out the
 * bytes go to the synth and the track asks the host for more. */
static void mpu_update_track(DWORD i)
{
    if (mpu.playbuf[i].type == T_MIDI_NORM || mpu.playbuf[i].type == T_MIDI_SYS)
        mpu_play_bytes(mpu.playbuf[i].value, mpu.playbuf[i].vlength);

    mpu.playbuf[i].vlength = 0;
    mpu.playbuf[i].type    = T_OVERFLOW;
    mpu.playbuf[i].counter = 0xF0;         /* re-arm far out; the host's next
                                            * timing byte replaces it */
    if (mpu.tmask & (1u << i)) mpu.req_mask |= (1u << i);
}

static void mpu_update_conductor(void)
{
    mpu.condbuf.vlength = 0;
    mpu.condbuf.type    = T_OVERFLOW;
    mpu.condbuf.counter = 0xF0;
    mpu.req_mask |= (1u << 9);             /* bit 9: conductor wants data */
}

/* One EOI's worth of host-bound requests: ask for the first thing that wants
 * attention. Called when the host finishes servicing an interrupt, and after
 * a tick that raised a new request. */
static void mpu_eoi_handler(void)
{
    DWORD i;
    if (mpu.queue_used) return;            /* host has not drained us yet */
    if (!mpu.req_mask)   return;

    for (i = 0; i < 16; i++) {
        if (!(mpu.req_mask & (1u << i))) continue;
        mpu.req_mask &= ~(1u << i);
        if (i == 13) {                     /* bit 13: the clock-to-host tick */
            mpu_q(MSG_CLOCK);
        } else if (i == 9) {
            mpu.cond_req = 1;
            mpu_q(MSG_COMMAND_REQ);
        } else {
            mpu.n_req++;
            mpu_q((BYTE)(MSG_DATA_REQ + i));
        }
        return;                            /* one request per interrupt */
    }
}

/* ------------------------------------------------------------------- clock
 * One MPU tick: age every active track and the conductor, and run the
 * clock-to-host divider. */
static void mpu_tick(void)
{
    DWORD i;
    mpu.ticks++;

    if (mpu.playing) {
        for (i = 0; i < MPU_TRACKS; i++) {
            if (!(mpu.amask & (1u << i))) continue;
            if (--mpu.playbuf[i].counter <= 0) mpu_update_track(i);
        }
        if (mpu.conductor && --mpu.condbuf.counter <= 0) mpu_update_conductor();
    }

    if (mpu.clock_to_host) {
        if (++mpu.cth_counter >= mpu.cth_rate) {
            mpu.cth_counter = 0;
            mpu.req_mask |= (1u << 13);
        }
    }
    if (mpu.req_mask && !mpu.queue_used) mpu_eoi_handler();
}

/* How long one MPU tick is, in PIT ticks. tempo_rel is the relative-tempo
 * knob, 40 = x1. */
static DWORD mpu_pit_per_tick(void)
{
    DWORD t = mpu.tempo * mpu.tempo_rel / 40;
    if (t < 8)   t = 8;
    if (t > 250) t = 250;
    return MPU_PIT_PER_MIN / (t * mpu.timebase);
}

/* The timer callback does NOT assume it ran on time: it asks the PIT how much
 * real time has passed and advances the clock by that many MPU ticks, keeping
 * the remainder. Measured on Win98, a VMM time-out asked for 10 ms comes back
 * at 11-12 ms and cannot go below about 5 ms at all, so a tick-per-callback
 * clock would run slow by a fifth and drift forever. */
static void mpu_clock_advance(void)
{
    DWORD now = sys_time_pit();
    DWORD per = mpu_pit_per_tick();
    DWORD elapsed, n;

    if (!per) return;
    elapsed = now - mpu.last_pit;          /* unsigned: wrap-safe */
    n = elapsed / per;
    if (!n) return;
    if (n > 64) {                          /* after a long stall, do not
                                            * spend minutes catching up */
        n = 64;
        mpu.last_pit = now;
    } else {
        mpu.last_pit += n * per;
    }
    while (n--) mpu_tick();
}

/* ------------------------------------------------------------------- reset */
/* NB: the diagnostic counters (n_irq, n_req, n_poll, saw_uart) deliberately
 * survive this - they describe what the session has seen, and a game resets
 * the card several times while starting up. */
static void mpu_reset_state(void)
{
    DWORD i;
    mpu.queue_pos = mpu.queue_used = 0;
    mpu.playing = mpu.conductor = mpu.cond_req = mpu.cond_set = 0;
    mpu.wsd = mpu.wsm = mpu.wsd_start = 0;
    mpu.track = 0;
    mpu.command_byte = 0;
    mpu.req_mask = 0;
    mpu.tmask = mpu.cmask = mpu.amask = 0;
    mpu.midi_mask = 0xFFFF;
    mpu.tempo = 100;
    mpu.timebase = 120;
    mpu.tempo_rel = 40;
    mpu.cth_rate = 60;
    mpu.cth_counter = 0;
    mpu.clock_to_host = 0;
    mpu.ticks = 0;
    for (i = 0; i < MPU_TRACKS; i++) {
        mpu.playbuf[i].counter = 0xF0;
        mpu.playbuf[i].vlength = 0;
        mpu.playbuf[i].type    = T_OVERFLOW;
    }
    mpu.condbuf.counter = 0xF0;
    mpu.condbuf.vlength = 0;
    mpu.condbuf.type    = T_OVERFLOW;
    mpu_irq_clear();
    mpu.last_pit = sys_time_pit();
}

/* ---------------------------------------------------------------- commands
 * Writes to 0x331. Everything is ACKed; the ones that return data queue the
 * ACK first and the data behind it. */
static void mpu_command(BYTE d)
{
    DWORD i;

    if (d >= 0xC2 && d <= 0xC8) {           /* timebase 48..192 */
        mpu.timebase = 48 + (d - 0xC2) * 24;
        mpu_q(MSG_ACK);
        return;
    }
    if (d >= 0xD0 && d <= 0xD7) {           /* host will send track data */
        mpu.track = d & 7;
        mpu.wsd = 1; mpu.wsm = 0; mpu.wsd_start = 1;
        mpu_q(MSG_ACK);
        return;
    }
    if (d >= 0xA0 && d <= 0xA7) {           /* read a play counter */
        mpu_q(MSG_ACK);
        mpu_q((mpu.cmask & (1u << (d & 7)))
                  ? (BYTE)mpu.playbuf[d & 7].counter : 0);
        return;
    }
    /* Only THESE take a parameter on the data port. Treating the whole
     * 0xE0-0xEF range as parameterised would swallow the game's next data
     * byte for the ones that take none, which silently corrupts whatever
     * track was being filled. */
    if (d == 0xE0 || d == 0xE1 || d == 0xE7 ||
        d == 0xEC || d == 0xED || d == 0xEE || d == 0xEF) {
        mpu.command_byte = d;
        mpu_q(MSG_ACK);
        return;
    }

    switch (d) {
    case 0x3F:                              /* enter UART ("dumb") mode */
        mpu.uart = 1;
        mpu.saw_uart = 1;
        mpu_q(MSG_ACK);
        return;
    case 0xFF:                              /* reset */
        mpu_reset_state();
        mpu.uart = 0;
        mpu_q(MSG_ACK);
        return;
    case 0xDF:                              /* host will send a system message */
        mpu.wsm = 1; mpu.wsd = 0;
        break;
    case 0x8E: mpu.cond_set = 0; break;     /* conductor off */
    case 0x8F: mpu.cond_set = 1; break;     /* conductor on */
    case 0x94: mpu.clock_to_host = 0; break;
    case 0x95:                              /* clock-to-host on: the clock has
                                             * to run even when not playing */
        mpu.clock_to_host = 1;
        mpu.cth_counter = 0;
        mpu.last_pit = sys_time_pit();
        mpu_timer_arm();
        break;
    case 0xB1: mpu.tempo_rel = 40; break;   /* relative tempo back to x1 */
    case 0xB8:                              /* clear play counters */
        mpu.req_mask = 0;
        for (i = 0; i < MPU_TRACKS; i++) {
            mpu.playbuf[i].counter = 0xF0;
            mpu.playbuf[i].type    = T_OVERFLOW;
            mpu.playbuf[i].vlength = 0;
        }
        mpu.condbuf.counter = 0xF0;
        mpu.conductor = mpu.cond_set;
        break;
    case 0xB9:                              /* clear play map */
        mpu.req_mask = 0;
        break;
    case 0xAB: mpu_q(MSG_ACK); mpu_q(0x00); return;
    case 0xAC: mpu_q(MSG_ACK); mpu_q(MPU_VERSION); return;
    case 0xAD: mpu_q(MSG_ACK); mpu_q(MPU_REVISION); return;
    case 0xAF: mpu_q(MSG_ACK); mpu_q((BYTE)mpu.tempo); return;
    default:
        break;
    }

    /* 0x01/0x02/0x03 are MIDI stop/start/continue, passed to the synth;
     * bits 2 and 3 of a command below 0x30 stop and start playback.
     *
     * RECORD is not emulated, and cannot be: recording means handing the host
     * MIDI that arrived at the card's input, and this bridge has no input -
     * it carries DOS programs' output to a synth, one way. The record
     * commands are ACKed like any other, so a program that starts recording
     * simply never receives anything, which is what a real card with nothing
     * plugged into its MIDI IN would do. */
    if (d < 0x30) {
        if (d == 0x01) mpu_play_byte(0xFC);
        if (d == 0x02) mpu_play_byte(0xFA);
        if (d == 0x03) mpu_play_byte(0xFB);
        if (d & 0x04) {                     /* stop playing */
            mpu.playing = 0;
            for (i = 0xB0; i < 0xC0; i++) { /* all notes off on every channel */
                mpu_play_byte((BYTE)i);
                mpu_play_byte(0x7B);
                mpu_play_byte(0x00);
            }
        }
        if (d & 0x08) {                     /* start playing */
            mpu.playing = 1;
            mpu.last_pit = sys_time_pit();
            mpu.req_mask = 0;
            mpu.amask = mpu.tmask;
            mpu.conductor = mpu.cond_set;   /* 0x8F before play means it runs */
            mpu.cond_req = 0;
            mpu.condbuf.type = T_OVERFLOW;
            mpu.condbuf.counter = 0xF0;
            mpu_timer_arm();
            ser_str("VOPL3: MPU play, tracks ");
            ser_dec(mpu.tmask);
            ser_str(" timebase ");
            ser_dec(mpu.timebase);
            ser_str(" tempo ");
            ser_dec(mpu.tempo);
            ser_str("\n");
        }
    }
    mpu_q(MSG_ACK);
}

/* How many DATA bytes follow a MIDI status byte. */
static DWORD mpu_msg_len(BYTE s)
{
    if (s <  0x80) return 0;
    if (s <  0xC0) return 2;      /* note off/on, aftertouch, controller */
    if (s <  0xE0) return 1;      /* program change, channel pressure    */
    if (s <  0xF0) return 2;      /* pitch bend                          */
    if (s == 0xF2) return 2;      /* song position                       */
    if (s == 0xF1 || s == 0xF3) return 1;
    return 0;
}

/* ------------------------------------------------------------- data writes
 * Writes to 0x330 while in intelligent mode: either the host answering a
 * data request (timing byte + event), or a system message. */
static void mpu_data(BYTE d)
{
    DWORD t;

    /* A command that asked for a parameter takes it HERE, on the data port -
     * the command itself was ACKed when it arrived, and the parameter is not
     * acknowledged again. */
    if (mpu.command_byte) {
        DWORD c = mpu.command_byte;
        mpu.command_byte = 0;
        switch (c) {
        case 0xE0: mpu.tempo = d ? d : 1; break;            /* tempo */
        case 0xE1: mpu.tempo_rel = d ? d : 40; break;       /* relative tempo */
        case 0xE7:                                          /* clock-to-host */
            mpu.cth_rate = d ? d : 60;
            mpu.cth_counter = 0;
            break;
        case 0xEC: mpu.tmask = d; break;                    /* active tracks */
        case 0xED: mpu.cmask = d; break;                    /* play counters */
        case 0xEE: mpu.midi_mask = (mpu.midi_mask & 0xFF00) | d; break;
        case 0xEF: mpu.midi_mask = (mpu.midi_mask & 0x00FF) | ((DWORD)d << 8); break;
        default:   break;
        }
        return;
    }

    /* "Send data to track" (0xD0+n): the host hands over ONE MIDI message,
     * which goes straight to the synth. It has to end when that message ends,
     * or every later data byte is swallowed by this path and track loading
     * never sees any of it. Sierra's SCI drivers use this as their whole
     * output path - they keep their own timing and push events one at a time,
     * which is why a game can drive intelligent mode hard and never ask the
     * card's sequencer to play anything. */
    if (mpu.wsd) {
        mpu_play_byte(d);
        if (mpu.wsd_start) {
            mpu.wsd_start = 0;
            if (d == 0xF0) {                /* SysEx: runs until 0xF7 */
                mpu.wsd = 0;
                mpu.wsm = 1;
                return;
            }
            if (d >= 0x80) {                /* a status byte of its own */
                mpu.wsd_status = d;
                mpu.wsd_len    = mpu_msg_len(d);
            } else {                        /* running status: this is data */
                mpu.wsd_len = mpu_msg_len(mpu.wsd_status);
                if (mpu.wsd_len) mpu.wsd_len--;
            }
            if (!mpu.wsd_len) mpu.wsd = 0;
            return;
        }
        if (mpu.wsd_len && !--mpu.wsd_len) mpu.wsd = 0;
        return;
    }
    if (mpu.wsm) {                          /* system message (SysEx) */
        mpu_play_byte(d);
        if (d == 0xF7) mpu.wsm = 0;
        return;
    }

    t = mpu.track;

    /* Conductor data: a timing byte, then one command byte. Both have to be
     * consumed here - letting the second one fall through would push it into
     * whichever track is current and corrupt that track's event.
     *
     * What the conductor carries (tempo changes, mostly) is accepted and NOT
     * acted on: no game seen drives it, and guessing at its semantics would
     * be worse than ignoring it. Staying in sync with the byte stream is the
     * part that matters. */
    if (mpu.cond_req) {
        if (mpu.condbuf.type == T_OVERFLOW) {      /* expecting the timing */
            if (d < 0xF0) {
                mpu.condbuf.counter = d;
                mpu.condbuf.type    = T_COMMAND;
                mpu.condbuf.vlength = 0;
            }
            return;
        }
        if (mpu.condbuf.vlength < sizeof(mpu.condbuf.value))
            mpu.condbuf.value[mpu.condbuf.vlength++] = d;
        mpu.cond_req = 0;                          /* one command per request */
        return;
    }

    if (mpu.playbuf[t].type == T_OVERFLOW) {
        if (d < 0xF0) {                     /* timing byte for this track */
            mpu.playbuf[t].counter = d;
            mpu.playbuf[t].type    = T_MIDI_NORM;
            mpu.playbuf[t].vlength = 0;
            return;
        }
        if (d == 0xF8) { mpu.playbuf[t].counter = 0xF0; return; }   /* overflow */
        if (d == 0xFC) {                    /* "all end" for this track */
            mpu.amask &= ~(1u << t);
            if (!mpu.amask) mpu_q(MSG_END);
            return;
        }
        return;
    }

    if (mpu.playbuf[t].vlength < sizeof(mpu.playbuf[t].value))
        mpu.playbuf[t].value[mpu.playbuf[t].vlength++] = d;
}

/* ------------------------------------------------------- the port interface */
static BYTE mpu_i_read_data(void)
{
    BYTE b = mpu_dequeue();
    if (!mpu.queue_used && mpu.req_mask) mpu_eoi_handler();
    return b;
}

/* 0x331 read: bit 0x80 clear = data to read, bit 0x40 clear = ready for a
 * write. We consume writes instantly, so DRR is always ready. */
static BYTE mpu_i_read_status(void)
{
    if (mpu.queue_used) mpu.n_poll++;   /* the game is collecting by polling */
    return (BYTE)(mpu.queue_used ? 0x00 : 0x80);
}

/* ===================== Win9x plumbing (VOPL3's own half) =================
 * The IRQ. VPICD hands a line to one owner, and there is no un-virtualize
 * call, so it is claimed once and kept. Two VPICD callbacks matter:
 *   EOI_Proc  - the guest's ISR acknowledged; clear the request and, if more
 *               is waiting, interrupt again (that is how the card keeps a
 *               conversation going).
 *   IRET_Proc - the guest returned from the ISR without an EOI (a crashed
 *               game, or a VM where nothing hooked the vector). Measured on
 *               Win98: without this, ONE unacknowledged interrupt leaves the
 *               line in service and VPICD silently drops every later raise.
 */
/* The PHYSICAL interrupt fired on a line we virtualized. There is no hardware
 * behind this emulation, so this should never happen - but if something on the
 * machine does raise it (we only get a line nobody else claimed, yet that is
 * not the same as nobody being able to assert it), the one thing we must not
 * do is nothing: without a physical EOI the line stays blocked for good, and
 * every lower-priority interrupt with it. Acknowledge and drop. */
static void __stdcall mpu_hw_int(DWORD h, DWORD vm)
{
    (void)vm;
    mpu.n_hw++;
    vpicd_phys_eoi(h);
}

static void __stdcall mpu_eoi_int(DWORD h, DWORD vm)
{
    mpu.irq_pending = 0;
    vpicd_clear_int(h, vm);
    if (mpu.queue_used) mpu_irq_raise();    /* still talking */
    else                mpu_eoi_handler();  /* anything else to ask for? */
}

static void __stdcall mpu_iret_int(DWORD h, DWORD vm)
{
    mpu.irq_pending = 0;                    /* never acknowledged - recover */
    vpicd_clear_int(h, vm);
}

void __declspec(naked) mpu_hw_thunk(void)
{
    _asm { pushad
           push ebx
           push eax
           call mpu_hw_int
           popad
           ret }
}

void __declspec(naked) mpu_eoi_thunk(void)
{
    _asm { pushad
           push ebx
           push eax
           call mpu_eoi_int
           popad
           ret }
}

void __declspec(naked) mpu_iret_thunk(void)
{
    _asm { pushad
           push ebx
           push eax
           call mpu_iret_int
           popad
           ret }
}

static void mpu_irq_raise(void)
{
    if (!mpu.irq_handle || !mpu.vm) return;
    if (mpu.irq_pending) return;            /* one at a time - a second raise
                                             * while the guest is still in its
                                             * ISR is simply lost */
    mpu.irq_pending = 1;
    mpu.n_irq++;
    vpicd_set_int(mpu.irq_handle, mpu.vm);
}

static void mpu_irq_clear(void)
{
    if (!mpu.irq_handle || !mpu.vm || !mpu.irq_pending) return;
    mpu.irq_pending = 0;
    vpicd_clear_int(mpu.irq_handle, mpu.vm);
}

/* Claim the IRQ the game is configured for. IRQ 2 cannot be served: VPICD
 * refuses the cascade, and the AT's INT 71h -> INT 0Ah compatibility chain
 * does not fire in a DOS box (measured), so a game set to "IRQ 2" would never
 * hear us - it has to be told to use 9 (or 5, 7, 10...). */
static DWORD mpu_irq_setup(DWORD irq, DWORD force)
{
    if (irq < 3 || irq > 15) return 0;
    if (mpu.irq_handle && mpu.irq == irq) return 1;   /* already ours */
    if (mpu.irq_handle) return 0;                     /* cannot change it */

    /* REFUSE A LINE THAT IS IN USE. VPICD does not protect us here: it will
     * hand over an IRQ a Windows device driver is already using, and from
     * then on that device's interrupts arrive at our callback instead of its
     * own. Measured, the hard way: configuring IRQ 7 on a machine whose
     * HD-Audio sits there hangs Windows as it boots, right about when the
     * audio stack comes up.
     *
     * An unmasked line at the physical PIC means something is listening on
     * it; an unused one is masked. This can only go on what the controller
     * shows at the moment we ask, but it turns "the machine no longer boots"
     * into "the interrupt was not claimed", which is the right way round. */
    if (!force) {
        BYTE  mask = (BYTE)((irq < 8) ? inp(0x21) : inp(0xA1));
        DWORD bit  = (irq < 8) ? irq : (irq - 8);
        if (!(mask & (1u << bit))) {
            mpu.irq_busy = 1;
            ser_str("VOPL3: MPU IRQ ");
            ser_dec(irq);
            ser_str(" is in use by something else - NOT claimed\n");
            return 0;
        }
    }

    mpu.desc.IRQ_Number       = (WORD)irq;
    mpu.desc.Options          = 0;
    mpu.desc.Hw_Int_Proc      = (DWORD)mpu_hw_thunk;  /* VPICD requires one */
    mpu.desc.Virt_Int_Proc    = 0;
    mpu.desc.EOI_Proc         = (DWORD)mpu_eoi_thunk;
    mpu.desc.Mask_Change_Proc = 0;
    mpu.desc.IRET_Proc        = (DWORD)mpu_iret_thunk;
    mpu.desc.IRET_Time_Out    = 500;                  /* ms before we give up */
    mpu.desc.Hw_Int_Ref       = 0;
    mpu.irq_handle = vpicd_virtualize(&mpu.desc);
    if (!mpu.irq_handle) {
        mpu.desc.Options = VPICD_OPT_CAN_SHARE;
        mpu.irq_handle = vpicd_virtualize(&mpu.desc);
    }
    if (mpu.irq_handle) mpu.irq = irq;
    ser_str("VOPL3: MPU intelligent IRQ ");
    ser_dec(irq);
    ser_str(mpu.irq_handle ? " OK\n" : " REFUSED\n");
    return mpu.irq_handle ? 1 : 0;
}

/* The clock. One VMM time-out at a time, re-armed while there is anything to
 * keep time for; mpu_clock_advance() works out how many MPU ticks really
 * elapsed, so the coarse and jittery interval does not become tempo error. */
#define MPU_TIMER_MS 5

void __declspec(naked) mpu_timer_thunk(void);

static void __stdcall mpu_timer_cb(DWORD vm_now)
{
    (void)vm_now;                           /* fires in whatever VM is current */
    mpu.timer_armed = 0;
    mpu_clock_advance();
    if (mpu.playing || mpu.clock_to_host) mpu_timer_arm();
}

void __declspec(naked) mpu_timer_thunk(void)
{
    _asm { pushad
           push ebx
           call mpu_timer_cb
           popad
           ret }
}

static void mpu_timer_arm(void)
{
    DWORD ms = MPU_TIMER_MS;
    void *cb = (void *)mpu_timer_thunk;
    if (mpu.timer_armed) return;
    mpu.timer_armed = 1;
    _asm {
        push ebx
        push esi
        push edx
        mov  eax, ms
        xor  edx, edx
        mov  esi, cb
    }
    VMMCall(Set_Global_Time_Out);
    _asm {
        pop  edx
        pop  esi
        pop  ebx
    }
}

/* ---------------------------------------------------------------- the API
 * What vopl3.c calls. mpu_i_enabled() decides, per access, whether the
 * intelligent engine or the old ACK-everything stub handles the port - so
 * turning the feature off restores the A08 behaviour exactly. */
static DWORD mpu_i_active(void) { return mpu.enabled; }   /* engine owns 330/331 */
static DWORD mpu_i_uart(void)   { return mpu.uart; }      /* ...but in UART mode */

/* For STAT: what this session has actually been asked to do.
 *   [0] bit0 engine on, bit1 a game asked for UART, bit2 in UART right now,
 *       bit3 playing, bits 8-15 the IRQ (0 = polled, none claimed)
 *   [1] interrupts raised   [2] data requests   [3] polled status reads */
static void mpu_i_stat(DWORD out[4])
{
    out[0] = (mpu.enabled ? 1u : 0u)
           | (mpu.saw_uart ? 2u : 0u)
           | (mpu.uart ? 4u : 0u)
           | (mpu.playing ? 8u : 0u)
           | (mpu.irq_busy ? 16u : 0u)
           | (mpu.irq_handle ? 32u : 0u)      /* the line is ours */
           | ((mpu.irq & 0xFF) << 8)          /* what was configured */
           | ((mpu.n_hw < 0xFFFF ? mpu.n_hw : 0xFFFF) << 16);
    out[1] = mpu.n_irq;
    out[2] = mpu.n_req;
    out[3] = mpu.n_poll;
}

/* irq = 0 means intelligent mode WITHOUT an interrupt: the engine answers and
 * keeps time exactly the same, it just never raises a line, so a game that
 * polls the status port works and no IRQ is claimed from the machine. Games
 * that want interrupts need the number they are configured for. */
static void mpu_i_enable(DWORD on, DWORD irq, DWORD force)
{
    ser_str("VOPL3: MPU intelligent mode ");
    if (!on) {
        ser_str("OFF\n");
        mpu.enabled = 0;
        return;
    }
    ser_str(irq ? "ON, irq " : "ON, polled (no irq)");
    if (irq) ser_dec(irq);
    ser_str("\n");            /* ser_ch exists only in -Serial builds */
    mpu.enabled = 1;
    if (irq) mpu_irq_setup(irq, force);
    mpu_reset_state();
    mpu.uart = 0;
}

/* Every port access remembers who is talking to us: the interrupts have to go
 * to that VM, and the renderer needs to know when its DOS box closes. */
static void mpu_i_set_vm(DWORD vm)
{
    mpu.vm = vm;
}
