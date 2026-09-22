# Building VOPL3

The four components are built with **Open Watcom 2.0** on a Windows host, driven
by the PowerShell `build.ps1` scripts. The build is self-contained: no network
access is needed and the only external dependency is the Open Watcom toolchain.

## 1. Prerequisites

- **Windows** with **PowerShell** (Windows PowerShell 5.1 is fine).
- **Open Watcom 2.0** (open source):
  <https://github.com/open-watcom/open-watcom-v2/releases>
  Open Watcom is the compiler/linker/assembler used throughout — it is one of the
  few modern toolchains that still targets 16/32-bit Windows and Win9x VxDs.

### Placing the toolchain

The scripts look for Open Watcom at **`tools/ow`** under the repo root, i.e. they
expect `tools/ow/binnt64/wcc386.exe` to exist. Put it there by copying your
Open Watcom install into `tools/ow`, or with a directory junction:

```powershell
# from the repo root, after installing Open Watcom (e.g. to C:\WATCOM):
New-Item -ItemType Directory tools | Out-Null
New-Item -ItemType Junction -Path tools\ow -Target C:\WATCOM
# sanity check:
Test-Path tools\ow\binnt64\wcc386.exe    # -> True
```

`tools/` is git-ignored — the toolchain is never committed.

## 2. Build

Run these from the repo root, in order:

```powershell
.\vxd\build.ps1          # -> vxd\vopl3.vxd         (~6 KB)
.\renderer\build.ps1     # -> renderer\voplsrv.exe  (~35 KB, Nuked OPL3)
                         #    renderer\voplfast.exe (~52 KB, Nuked-OPL3-fast)
                         #    renderer\vopldb.exe   (~84 KB, DOSBox DBOPL)
.\gui\build.ps1          # -> gui\voplcfg.exe       (~35 KB, the control panel)
.\installer\build.ps1    # -> installer\dist\       (the shippable package)
```

- **`vxd\build.ps1`** compiles `vopl3.c` (`wcc386`), links a raw VxD (`wlink`),
  then post-processes the LE image. Two `wlink` quirks are patched automatically
  or the VxD would load but silently do nothing (see the "VxD toolchain" note in
  [README.md](README.md)): the *ModuleFlags* "no internal fixups" bit is cleared,
  and the DDB export bundle is converted from a 286 call-gate to a 32-bit entry.
  `fixlink.exe` is built on first run from `ref/vmdisp9x/fixlink/fixlink.c`.
  - Add **`-Serial`** to compile in COM1 debug tracing (`-DVOPL3_SERIAL`); it is
    off by default and costs nothing when off.
- **`renderer\build.ps1`** compiles `voplsrv.c` three times — with Nuked OPL3
  (`nuked-opl3/opl3.c` → `voplsrv.exe`), with Nuked-OPL3-fast
  (`nuked-opl3-fast/opl3.c` → `voplfast.exe`; bit-exact output, ~2x less CPU),
  and with DOSBox's DBOPL (`dbopl/dbopl.cpp` + `dbopl/dbopl_glue.cpp` →
  `vopldb.exe`; C++, built by `wcl386` via `wpp386`; far less CPU, less
  accurate, GPL v2+ as a whole) — and links `winmm` + `advapi32` (the latter
  for the registry read that gates the optional MIDI bridge). All build as
  **GUI-subsystem** apps (`-l=nt_win`,
  `WinMain`) so they run hidden with no console window. All are compiled with
  full optimization (`-otexan -6r -fp6`); Watcom's default is *no* optimization,
  which makes the synthesis ~2.3x slower — enough to peg a P3-class CPU.
- **`gui\build.ps1`** builds the control panel `voplcfg.exe` (GUI subsystem;
  links `shell32` for the tray, `comctl32` for the trackbar, `winmm` for MIDI
  device enumeration, and `advapi32`). It shares `renderer/vopl3ipc.h` — the
  renderer status/control contract — and attaches the app icon
  (`resource.rc` → `voplcfg.ico`) with `wrc` after linking.
- **`installer\build.ps1`** builds `SBPATCH.EXE` and `VOPLSTOP.EXE` from their
  `.C` sources and `MIDILIST.EXE` from `tests/MIDILIST.C`, then assembles
  `installer/dist/` — the VxD, the three renderer builds (+ `DBOPL-LICENSE.txt`,
  the GPL text that must travel with `VOPLDB.EXE`), `VOPLCFG.EXE`,
  `SBPATCH.EXE`, `VOPLSTOP.EXE`, `MIDILIST.EXE`, and the CRLF-normalized
  `INSTALL/UNINSTALL` `.BAT` + `.REG` (incl. `MIDION.REG`, `FMSBEMUL.REG`,
  `FMFREE.REG`, `VOPLCFG.REG`) +
  `README.TXT` + `VOPL3.INI`. **`dist/` is the folder you copy to the Win98/ME
  machine.** `INSTALL.BAT` asks who handles the FM ports and the MIDI ports,
  which renderer build to install (if VOPL3 plays FM), and whether the
  control panel autostarts with Windows; the chosen renderer lands as
  `C:\VOPL3\VOPLSRV.EXE`.

Build outputs (`vxd/vopl3.vxd`, `renderer/voplsrv.exe`, `renderer/voplfast.exe`,
`renderer/vopldb.exe`,
`installer/dist/`) are not committed — the build is deterministic (apart from
the renderer `.exe`s' embedded PE build-timestamp), so build them with the
steps above.

## 3. Install (on the Windows 98/ME machine)

Copy `installer/dist/` to the target and run `INSTALL.BAT` from a DOS box. It
installs the VxD (boot-loaded), installs the renderer (autostarts, hidden), and
sets up SBEMUL to leave the chosen ports alone (its `SoftFM` registry value for
FM, a patch to `SBEMUL.SYS` for MIDI). Reboot. In your DOS game
set **Music = AdLib/OPL3** or **General MIDI** and **Sound FX = Sound Blaster**.
`UNINSTALL.BAT` reverts
everything (restores the original `SBEMUL.SYS`, removes VOPL3). See
[installer/README.TXT](installer/README.TXT) for details.

## Test programs (optional)

`tests/` holds standalone diagnostics, each with its own `build-*.ps1`. Most build
with the **same Open Watcom** toolchain as above (`tools/ow`):

- `OPLTUNE.C` — DOS OPL3 FM player that yields the CPU between notes (isolates
  renderer CPU-starvation from buffering).
- `OPLWIN32.C` — Win32 ring-3 probe: writes OPL registers to 0x388–0x38B from a
  normal Windows process, to confirm the VxD trap catches more than DOS boxes.
- `SBTEST.C` — probes the SoundBlaster DSP (checks SBEMUL's digital side is alive).
- `VOPLSTAT.C` — reads the VxD ring stats (proves it captured FM writes).
- `host/oplrender.c` — host-side Nuked OPL3 render harness (WAV out; runs on your
  build machine, not the Win98 target).

The one exception is **`ADLIBTST.ASM`** (a DOS `.COM` that characterizes what is
actually at the OPL ports — real chip vs SBEMUL's fake trap vs VOPL3). It is NASM
syntax, so `build-adlibtst.ps1` needs **NASM** (<https://www.nasm.us/>): put
`nasm.exe` at `tools/nasm/nasm.exe` or on `PATH`.

## Third-party build inputs

| What | Where | License |
|---|---|---|
| Nuked OPL3 (`opl3.c/.h`) | `nuked-opl3/` | LGPL 2.1 (`nuked-opl3/LICENSE`) |
| Nuked-OPL3-fast (`opl3.c/.h`, `wf_rom.h`) | `nuked-opl3-fast/` | LGPL 2.1 (`nuked-opl3-fast/LICENSE`) |
| DBOPL (`dbopl.cpp/.h`, DOSBox SVN r4494, + Open Watcom patch) | `dbopl/` | GPL v2 or later (`dbopl/COPYING`) |
| `fixlink` + VxD glue headers (`vmm.h`, `io32.h`, `code32.h`) | `ref/vmdisp9x/fixlink/`, `vxd/` | MIT (`ref/vmdisp9x/LICENSE`) |
| Open Watcom 2.0 | `tools/ow` (not committed) | Sybase Open Watcom Public License |
