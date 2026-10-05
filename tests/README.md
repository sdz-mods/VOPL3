# VOPL3 test / diagnostic programs

Standalone tools used while developing VOPL3. **None of these are needed to build
or use VOPL3** — they're here for troubleshooting, characterizing hardware, and
verifying the pipeline. Each has its own `build-*.ps1`; build outputs (`.EXE`,
`.COM`) are git-ignored, not committed.

| Program | Runs on | What it does | Build |
|---|---|---|---|
| `ADLIBTST.ASM` | Win9x DOS box | Characterizes what is actually at the OPL ports (real chip vs SBEMUL's fake trap vs VOPL3): raw port reads, AdLib presence detection, OPL2/OPL3 signature, timer-period measurement. Writes `REPORT.TXT`. | `build-adlibtst.ps1` (**NASM**) |
| `MTDISP.C` | Win9x DOS box | Writes a message to an MT-32's front-panel display through the MPU-401 port: one SysEx, nothing else, so the display is the receipt. Notes and SysEx leave Windows by two different calls (`midiOutShortMsg` and `midiOutLongMsg`) and the second can fail while music plays perfectly — a game cannot tell those apart, this can. Three modes cover the paths: UART, intelligent-mode `0xDF`, and intelligent-mode `0xD0` send-data (what Sierra's SCI drivers use). Prints the bytes it pushed and writes `C:\MTDISP.LOG`; read the result off the control panel's `sysex=` figures. | `build-mtdisp.ps1` |
| `MPUTEST.C` | Win9x DOS box | Drives VOPL3's MPU-401 **intelligent mode** the way a game's own driver would: resets the card, checks it answers as an intelligent MPU (version 15h), hooks the IRQ, sets timebase and tempo, starts the on-board sequencer and answers its data requests with notes — then exercises the conductor track and a command that takes no parameter. Reports interrupt and clock-message counts and the measured clock interval, and writes `C:\MPUTEST.LOG`. Notes go out on MIDI channel 2, where a stock MT-32 puts Part 1. | `build-mputest.ps1` |
| `OPLHANG.C` | Win9x DOS box | Keys on a sustaining OPL3 chord (on both register banks) and exits without a key-off, like a game that quits without silencing the chip. Close the DOS box afterwards: VOPL3 keys the notes off, so the chord fades out instead of playing on. `OPLHANG /OFF` keys off every voice. | `build-oplhang.ps1` |
| `OPLTUNE.C` | Win9x DOS box | Plays a looping OPL3 arpeggio but *yields the CPU* between notes. If this is smooth while a game stutters, the bottleneck is CPU starvation of the renderer, not buffering. | `build-opltune.ps1` |
| `OPLWIN32.C` | Win9x (Win32) | Writes OPL registers to 0x388–0x38B from a normal ring-3 Win32 process, to confirm the VxD trap catches more than DOS boxes. Plays a scale. | `build-oplwin32.ps1` |
| `SBTEST.C` | Win9x DOS box | Resets the SoundBlaster DSP (0x220) and reads its version, to check SBEMUL's digital side is still alive (e.g. before/after installing VOPL3). | `build-sbtest.ps1` |
| `VOPLSTAT.C` | Win9x (Win32) | Reads VOPL3's ring stats over `\\.\VOPL3` and writes `C:\VOPLSTAT.TXT` (`head > 0` = the VxD captured FM writes). | `build-voplstat.ps1` |
| `host/oplrender.c` | your build PC | Host-side Nuked OPL3 render harness: renders test tones / register scripts / DOSBox `.dro` captures to a WAV. Useful as a golden reference off the target machine. | `host/build.ps1` |

## Toolchains

- **Open Watcom 2.0** (`tools/ow`, same as the main build — see [../BUILD.md](../BUILD.md))
  builds the C programs: the DOS ones (`OPLHANG`, `OPLTUNE`, `SBTEST`, `MPUTEST`, `MTDISP`) as 16-bit real-mode
  `.EXE`, the Win32 ones (`OPLWIN32`, `VOPLSTAT`) as 32-bit.
- **NASM** (<https://www.nasm.us/>) builds `ADLIBTST.ASM` into a DOS `.COM`. Put
  `nasm.exe` at `tools/nasm/nasm.exe` or on `PATH`.
- **gcc** builds the host harness `host/oplrender.c` (it runs on your PC, not the
  Win98 target). `host/build.ps1` finds gcc on `PATH`.

Run the DOS/Win32 programs on a Windows 98/ME machine with VOPL3 installed (and,
for the ones that make sound, the renderer running).
