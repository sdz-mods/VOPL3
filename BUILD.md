# Building VOPL3

The three components are built with **Open Watcom 2.0** on a Windows host, driven
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
.\vxd\build.ps1          # -> vxd\vopl3.vxd        (~5515 bytes)
.\renderer\build.ps1     # -> renderer\voplsrv.exe (~30720 bytes)
.\installer\build.ps1    # -> installer\dist\      (the shippable package)
```

- **`vxd\build.ps1`** compiles `vopl3.c` (`wcc386`), links a raw VxD (`wlink`),
  then post-processes the LE image. Two `wlink` quirks are patched automatically
  or the VxD would load but silently do nothing (see the "VxD toolchain" note in
  [README.md](README.md)): the *ModuleFlags* "no internal fixups" bit is cleared,
  and the DDB export bundle is converted from a 286 call-gate to a 32-bit entry.
  `fixlink.exe` is built on first run from `ref/vmdisp9x/fixlink/fixlink.c`.
  - Add **`-Serial`** to compile in COM1 debug tracing (`-DVOPL3_SERIAL`); it is
    off by default and costs nothing when off.
- **`renderer\build.ps1`** compiles `voplsrv.c` together with Nuked OPL3
  (`nuked-opl3/opl3.c`) and links `winmm`. It builds as a **GUI-subsystem** app
  (`-l=nt_win`, `WinMain`) so it runs hidden with no console window.
- **`installer\build.ps1`** builds `SBPATCH.EXE` from `SBPATCH.C` and assembles
  `installer/dist/` — the VxD, the renderer, `SBPATCH.EXE`, and the CRLF-normalized
  `INSTALL/UNINSTALL` `.BAT`/`.REG` + `README.TXT`. **`dist/` is the folder you
  copy to the Win98/ME machine.**

Build outputs (`vxd/vopl3.vxd`, `renderer/voplsrv.exe`, `installer/dist/`) are not
committed — the build is deterministic (apart from the renderer `.exe`'s embedded
PE build-timestamp), so build them with the steps above.

## 3. Install (on the Windows 98/ME machine)

Copy `installer/dist/` to the target and run `INSTALL.BAT` from a DOS box. It
installs the VxD (boot-loaded), installs the renderer (autostarts, hidden), and
patches `SBEMUL.SYS` to free FM port 0x388. Reboot. In your DOS game set
**Music = AdLib/OPL3** and **Sound FX = Sound Blaster**. `UNINSTALL.BAT` reverts
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
| `fixlink` + VxD glue headers (`vmm.h`, `io32.h`, `code32.h`) | `ref/vmdisp9x/fixlink/`, `vxd/` | MIT (`ref/vmdisp9x/LICENSE`) |
| Open Watcom 2.0 | `tools/ow` (not committed) | Sybase Open Watcom Public License |
