; ADLIBTST.COM - probe AdLib/OPL port behavior inside a Windows 9x DOS box
; Purpose: characterize SBEMUL.SYS's existing trap on port 388h
;   - raw reads of 388h/38Ah/220h/228h (0FFh usually means "nothing there")
;   - standard AdLib presence detection (timer 1 method)
;   - OPL2 vs OPL3 status signature (status bits 1-2)
;   - timer 1 / timer 2 period measurement in calibrated poll iterations
;   - 2 second test tone (audible only if something actually synthesizes FM)
; Output: screen + REPORT.TXT in current directory.
;
; Build: nasm -f bin ADLIBTST.ASM -o ADLIBTST.COM
;
; Interpreting the numbers (host side):
;   CAL9TICKS = poll-loop iterations in 9 BIOS ticks (494,325 us)
;   T1ITERS   = iterations until timer1 flag with latch F0h (nominal 1,280 us)
;   T2ITERS   = iterations until timer2 flag with latch F0h (nominal 5,120 us)
;   measured_us = ITERS * 494325 / CAL9TICKS  -> compare against nominal

        cpu 8086
        org 100h

start:
        mov     si, m_banner
        call    pstr

        ; ---------- raw port reads ----------
        mov     si, m_raw388
        call    pstr
        mov     dx, 388h
        in      al, dx
        call    phex
        call    pnl

        mov     si, m_raw38a
        call    pstr
        mov     dx, 38Ah
        in      al, dx
        call    phex
        call    pnl

        mov     si, m_raw220
        call    pstr
        mov     dx, 220h
        in      al, dx
        call    phex
        call    pnl

        mov     si, m_raw228
        call    pstr
        mov     dx, 228h
        in      al, dx
        call    phex
        call    pnl

        ; ---------- classic AdLib detection (timer 1) ----------
        mov     al, 04h                 ; reg 4: mask both timers
        mov     ah, 60h
        call    oplw
        mov     al, 04h                 ; reg 4: reset flags
        mov     ah, 80h
        call    oplw
        mov     dx, 388h
        in      al, dx
        mov     [s1], al
        mov     al, 02h                 ; reg 2: timer1 latch = FFh (1 tick = 80us)
        mov     ah, 0FFh
        call    oplw
        mov     al, 04h                 ; reg 4: start timer1, mask timer2
        mov     ah, 21h
        call    oplw
        mov     cx, 300                 ; > 80us worth of trapped port reads
        mov     dx, 388h
.dly:   in      al, dx
        loop    .dly
        in      al, dx
        mov     [s2], al
        mov     al, 04h                 ; stop + reset
        mov     ah, 60h
        call    oplw
        mov     al, 04h
        mov     ah, 80h
        call    oplw

        mov     si, m_s1
        call    pstr
        mov     al, [s1]
        call    phex
        mov     si, m_s2
        call    pstr
        mov     al, [s2]
        call    phex
        call    pnl

        mov     al, [s1]
        and     al, 0E0h
        jnz     .absent
        mov     al, [s2]
        and     al, 0E0h
        cmp     al, 0C0h
        jne     .absent
        mov     si, m_present
        call    pstr
        jmp     short .opl3chk
.absent:
        mov     si, m_absent
        call    pstr
.opl3chk:
        mov     al, [s1]
        and     al, 06h
        jnz     .isopl2
        mov     si, m_opl3
        call    pstr
        jmp     short .cal
.isopl2:
        mov     si, m_opl2
        call    pstr

        ; ---------- calibrate poll loop against BIOS ticks ----------
.cal:
        mov     si, m_calrun
        call    pstr
        call    tick                    ; sync to a tick edge
.sync:  mov     bx, ax
        call    tick
        cmp     ax, bx
        je      .sync
        mov     [t0], ax
        xor     ax, ax
        mov     [cnt], ax
        mov     [cnt+2], ax
        mov     dx, 388h
.calp:  in      al, dx                  ; same body as the timer poll loops
        add     word [cnt], 1
        adc     word [cnt+2], 0
        call    tick
        sub     ax, [t0]
        cmp     ax, 9
        jb      .calp
        mov     si, m_cal
        call    pstr
        mov     ax, [cnt]
        mov     dx, [cnt+2]
        call    pu32
        call    pnl

        ; ---------- timer 1 period (latch F0h -> nominal 1280us) ----------
        mov     al, 04h
        mov     ah, 60h
        call    oplw
        mov     al, 04h
        mov     ah, 80h
        call    oplw
        mov     al, 02h
        mov     ah, 0F0h
        call    oplw
        mov     al, 04h
        mov     ah, 21h                 ; start timer1
        call    oplw
        xor     ax, ax
        mov     [cnt], ax
        mov     [cnt+2], ax
        mov     dx, 388h
.t1p:   in      al, dx
        test    al, 40h                 ; timer1 flag
        jnz     .t1hit
        add     word [cnt], 1
        adc     word [cnt+2], 0
        cmp     word [cnt+2], 20h       ; ~2M iteration timeout
        jb      .t1p
        mov     si, m_t1to
        call    pstr
        jmp     short .t1end
.t1hit: mov     si, m_t1
        call    pstr
        mov     ax, [cnt]
        mov     dx, [cnt+2]
        call    pu32
        call    pnl
.t1end:
        mov     al, 04h
        mov     ah, 60h
        call    oplw
        mov     al, 04h
        mov     ah, 80h
        call    oplw

        ; ---------- timer 2 period (latch F0h -> nominal 5120us) ----------
        mov     al, 03h
        mov     ah, 0F0h
        call    oplw
        mov     al, 04h
        mov     ah, 42h                 ; start timer2, mask timer1
        call    oplw
        xor     ax, ax
        mov     [cnt], ax
        mov     [cnt+2], ax
        mov     dx, 388h
.t2p:   in      al, dx
        test    al, 20h                 ; timer2 flag
        jnz     .t2hit
        add     word [cnt], 1
        adc     word [cnt+2], 0
        cmp     word [cnt+2], 20h
        jb      .t2p
        mov     si, m_t2to
        call    pstr
        jmp     short .t2end
.t2hit: mov     si, m_t2
        call    pstr
        mov     ax, [cnt]
        mov     dx, [cnt+2]
        call    pu32
        call    pnl
.t2end:
        mov     al, 04h
        mov     ah, 60h
        call    oplw
        mov     al, 04h
        mov     ah, 80h
        call    oplw

        ; ---------- 2 second test tone (AdLib manual example patch) ----------
        mov     si, m_tone
        call    pstr
        mov     al, 20h                 ; modulator: mult 1
        mov     ah, 01h
        call    oplw
        mov     al, 40h                 ; modulator level
        mov     ah, 10h
        call    oplw
        mov     al, 60h                 ; modulator AD
        mov     ah, 0F0h
        call    oplw
        mov     al, 80h                 ; modulator SR
        mov     ah, 77h
        call    oplw
        mov     al, 23h                 ; carrier: mult 1
        mov     ah, 01h
        call    oplw
        mov     al, 43h                 ; carrier level max
        mov     ah, 00h
        call    oplw
        mov     al, 63h                 ; carrier AD
        mov     ah, 0F0h
        call    oplw
        mov     al, 83h                 ; carrier SR
        mov     ah, 77h
        call    oplw
        mov     al, 0A0h                ; f-number low
        mov     ah, 98h
        call    oplw
        mov     al, 0B0h                ; key on, block 4
        mov     ah, 31h
        call    oplw
        call    tick                    ; hold ~2s (36 BIOS ticks)
        mov     bx, ax
.tone:  call    tick
        sub     ax, bx
        cmp     ax, 36
        jb      .tone
        mov     al, 0B0h                ; key off
        mov     ah, 11h
        call    oplw
        mov     si, m_done
        call    pstr

        ; ---------- write REPORT.TXT ----------
        mov     ah, 3Ch
        xor     cx, cx
        mov     dx, fname
        int     21h
        jc      .nofile
        mov     bx, ax
        mov     ah, 40h
        mov     cx, [bufptr]
        sub     cx, buf
        mov     dx, buf
        int     21h
        mov     ah, 3Eh
        int     21h
        mov     si, m_wrote
        call    pstr
        jmp     short .exit
.nofile:
        mov     si, m_nowrite
        call    pstr
.exit:
        int     20h

; ---------------- routines ----------------

; write OPL register: AL = index, AH = data (dest 388h/389h)
oplw:
        push    dx
        push    cx
        push    ax
        mov     dx, 388h
        out     dx, al
        mov     cx, 6                   ; >3.3us settle
.w1:    in      al, dx
        loop    .w1
        pop     ax
        push    ax
        mov     al, ah
        inc     dx
        out     dx, al
        dec     dx
        mov     cx, 35                  ; >23us settle
.w2:    in      al, dx
        loop    .w2
        pop     ax
        pop     cx
        pop     dx
        ret

; BIOS tick counter low word -> AX
tick:
        push    es
        mov     ax, 0040h
        mov     es, ax
        mov     ax, [es:006Ch]
        pop     es
        ret

; emit char in AL to screen and capture buffer (preserves all registers)
emit:
        push    ax
        push    bx
        push    dx
        mov     bx, [bufptr]
        cmp     bx, bufend
        jae     .noroom
        mov     [bx], al
        inc     word [bufptr]
.noroom:
        mov     dl, al
        mov     ah, 02h
        int     21h
        pop     dx
        pop     bx
        pop     ax
        ret

; print zero-terminated string at SI
pstr:
        push    ax
        push    si
.next:  lodsb
        or      al, al
        jz      .fin
        call    emit
        jmp     short .next
.fin:   pop     si
        pop     ax
        ret

; CRLF
pnl:
        push    ax
        mov     al, 0Dh
        call    emit
        mov     al, 0Ah
        call    emit
        pop     ax
        ret

; print AL as two hex digits
phex:
        push    ax
        push    cx
        mov     ah, al
        mov     cl, 4
        shr     al, cl
        call    .dig
        mov     al, ah
        and     al, 0Fh
        call    .dig
        pop     cx
        pop     ax
        ret
.dig:   push    ax
        add     al, '0'
        cmp     al, '9'
        jbe     .out
        add     al, 7
.out:   call    emit
        pop     ax
        ret

; print DX:AX as unsigned decimal
pu32:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        mov     bx, 10
        xor     cx, cx
.div:   push    ax
        mov     ax, dx
        xor     dx, dx
        div     bx                      ; AX = high quotient, DX = rem
        mov     si, ax
        pop     ax
        div     bx                      ; AX = low quotient, DX = digit
        push    dx
        inc     cx
        mov     dx, si                  ; quotient now DX:AX
        push    ax
        or      ax, dx
        pop     ax
        jnz     .div
.prt:   pop     ax
        add     al, '0'
        call    emit
        loop    .prt
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; ---------------- data ----------------

m_banner  db 'ADLIBTST 1.0 - OPL port probe for Win9x DOS boxes', 0Dh, 0Ah
          db '(characterizes SBEMUL.SYS 388h trap / real OPL / our VxD)', 0Dh, 0Ah, 0
m_raw388  db 'RAW388=', 0
m_raw38a  db 'RAW38A=', 0
m_raw220  db 'RAW220=', 0
m_raw228  db 'RAW228=', 0
m_s1      db 'S1=', 0
m_s2      db ' S2=', 0
m_present db 'DETECT=PASS (AdLib presence check succeeds)', 0Dh, 0Ah, 0
m_absent  db 'DETECT=FAIL (no OPL visible at 388h)', 0Dh, 0Ah, 0
m_opl3    db 'STATUSTYPE=OPL3 (status bits 1-2 clear)', 0Dh, 0Ah, 0
m_opl2    db 'STATUSTYPE=OPL2 (status bits 1-2 set)', 0Dh, 0Ah, 0
m_calrun  db 'Calibrating poll loop (~0.5s)...', 0Dh, 0Ah, 0
m_cal     db 'CAL9TICKS=', 0
m_t1      db 'T1ITERS=', 0
m_t1to    db 'T1ITERS=TIMEOUT (flag never set)', 0Dh, 0Ah, 0
m_t2      db 'T2ITERS=', 0
m_t2to    db 'T2ITERS=TIMEOUT (flag never set)', 0Dh, 0Ah, 0
m_tone    db 'Playing 2s test tone (audible only if FM is synthesized)...', 0Dh, 0Ah, 0
m_done    db 'Tone done.', 0Dh, 0Ah, 0
m_wrote   db 'REPORT.TXT written.', 0Dh, 0Ah, 0
m_nowrite db 'Could not write REPORT.TXT!', 0Dh, 0Ah, 0
fname     db 'REPORT.TXT', 0

s1        db 0
s2        db 0
t0        dw 0
cnt       dd 0
bufptr    dw buf

buf:                                    ; capture buffer to end of segment
bufend    equ buf + 2048
