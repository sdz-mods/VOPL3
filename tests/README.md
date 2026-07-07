# VOPL3 test / diagnostic programs

Standalone tools used while developing VOPL3. **None of these are needed to build
or use VOPL3** — they're here for troubleshooting, characterizing hardware, and
verifying the pipeline. Each has its own `build-*.ps1`; build outputs (`.EXE`,
`.COM`) are git-ignored, not committed.

| Program | Runs on | What it does | Build |
|---|---|---|---|
| `ADLIBTST.ASM` | Win9x DOS box | Characterizes what is actually at the OPL ports (real chip vs SBEMUL's fake trap vs VOPL3): raw port reads, AdLib presence detection, OPL2/OPL3 signature, timer-period measurement. Writes `REPORT.TXT`. | `build-adlibtst.ps1` (**NASM**) |
| `OPLTUNE.C` | Win9x DOS box | Plays a looping OPL3 arpeggio but *yields the CPU* between notes. If this is smooth while a game stutters, the bottleneck is CPU starvation of the renderer, not buffering. | `build-opltune.ps1` |
| `OPLWIN32.C` | Win9x (Win32) | Writes OPL registers to 0x388–0x38B from a normal ring-3 Win32 process, to confirm the VxD trap catches more than DOS boxes. Plays a scale. | `build-oplwin32.ps1` |
| `SBTEST.C` | Win9x DOS box | Resets the SoundBlaster DSP (0x220) and reads its version, to check SBEMUL's digital side is still alive (e.g. before/after installing VOPL3). | `build-sbtest.ps1` |
| `VOPLSTAT.C` | Win9x (Win32) | Reads VOPL3's ring stats over `\\.\VOPL3` and writes `C:\VOPLSTAT.TXT` (`head > 0` = the VxD captured FM writes). | `build-voplstat.ps1` |
| `host/oplrender.c` | your build PC | Host-side Nuked OPL3 render harness: renders test tones / register scripts / DOSBox `.dro` captures to a WAV. Useful as a golden reference off the target machine. | `host/build.ps1` |

## Toolchains

- **Open Watcom 2.0** (`tools/ow`, same as the main build — see [../BUILD.md](../BUILD.md))
  builds the C programs: the DOS ones (`OPLTUNE`, `SBTEST`) as 16-bit real-mode
  `.EXE`, the Win32 ones (`OPLWIN32`, `VOPLSTAT`) as 32-bit.
- **NASM** (<https://www.nasm.us/>) builds `ADLIBTST.ASM` into a DOS `.COM`. Put
  `nasm.exe` at `tools/nasm/nasm.exe` or on `PATH`.
- **gcc** builds the host harness `host/oplrender.c` (it runs on your PC, not the
  Win98 target). `host/build.ps1` finds gcc on `PATH`.

Run the DOS/Win32 programs on a Windows 98/ME machine with VOPL3 installed (and,
for the ones that make sound, the renderer running).
