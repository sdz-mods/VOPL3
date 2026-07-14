VOPL3 - OPL3 FM emulation for Windows 98/ME
===========================================

For Windows 98/ME. Adds OPL3 FM synthesis (Nuked OPL3) to DOS boxes,
coexisting with Microsoft's SBEMUL - the standard Win98 DOS Sound Blaster
emulation, which gives digital audio + MIDI but NO FM. The OPL3 audio plays
through the normal Windows audio output (waveOut), so its output is not tied
to any particular sound card.

VOPL3 is meant for systems whose audio hardware has no FM synthesizer of its
own and rely on SBEMUL. If your sound card already provides hardware OPL3/FM
(e.g. some Yamaha YMF7xx cards), DOS FM already works through the card and its
driver usually owns ports 388-38B itself - in that case you do NOT need VOPL3.

INSTALL
  Run INSTALL.BAT from Windows (in an MS-DOS Prompt / DOS box), from the folder
  that contains these files. It:
    - installs VOPL3.VXD (C:\WINDOWS\SYSTEM, loads at boot),
    - installs the renderer (C:\VOPL3\VOPLSRV.EXE, autostarts at login).
      You choose between two builds with identical sound output:
        1 = Nuked OPL3 (reference), 2 = Nuked OPL3 fast (about half the
        CPU use - recommended, especially on slower machines),
    - installs the control panel (C:\VOPL3\VOPLCFG.EXE; you choose whether
      it starts with Windows, minimized to the system tray),
    - asks FM only vs FM + MIDI (see MIDI below; default FM only),
    - patches SBEMUL.SYS so it stops claiming FM port 388 (keeps digital+MIDI).
  Then REBOOT.

  If it says SBEMUL.SYS is "in use", finish that one step from MS-DOS mode
  (Start > Shut Down > Restart in MS-DOS mode), change to this folder and run:
      SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS

USE
  After reboot the renderer starts automatically. In your DOS game set
  Music = AdLib (or OPL3) and Sound FX = Sound Blaster. You then get OPL3
  music (via VOPL3) AND digital sound effects (via SBEMUL) at the same time.

CONTROL PANEL (VOPLCFG.EXE)
  A small tray app for configuring and watching VOPL3 - no reboot needed:
    - pick the MIDI output device and set the FM volume; Apply saves them to
      VOPL3.INI and the running renderer picks them up immediately,
    - shows engine status (renderer/backend, FM playing/idle, priority,
      MIDI bridge + synth state), driver + renderer revisions, and the same
      debug counters VOPLSTAT prints.
  Minimize hides it to the system tray; the X button exits it. Everything
  works exactly the same with the control panel not running - it is a
  convenience, not a required part.

FM VOLUME
  The FM music volume is easiest to change in the control panel (applies
  live). It is stored in C:\VOPL3\VOPL3.INI (volume=<percent>, default 200,
  max 400). 100 is the OPL3 chip's authentic digital level, which sounds
  quiet next to SBEMUL's digital sound.

MIDI (optional - only if you chose FM + MIDI at install)
  Normally DOS-game MIDI plays through SBEMUL's fixed synth (Microsoft GS
  Wavetable). Choosing FM + MIDI makes VOPL3 take over MPU-401 MIDI and send
  it through the Windows MIDI Mapper, so it can play on ANY installed synth or
  device - a software synth (Roland VSC, Yamaha S-YXG50, ...), a hardware
  wavetable, or an external module on a MIDI interface.
    - Pick the target in the VOPL3 control panel (VOPLCFG.EXE - easiest,
      applies live), in Control Panel > Multimedia > MIDI, or in
      C:\VOPL3\VOPL3.INI  [midi] device=  (65535 = MIDI Mapper (default), or a
      device index). Run MIDILIST.EXE to list devices and their indices.
    - FM + MIDI replaces SBEMUL's MIDI. FM only leaves it untouched.
    - Only UART-mode MPU-401 is supported.

UNINSTALL
  Run UNINSTALL.BAT (restores the original SBEMUL.SYS, removes VOPL3), reboot.

ABOUT THE SBEMUL PATCH
  SBEMUL claims the AdLib FM port 388 only to fake AdLib *detection* - it makes
  no FM sound - and it tears down its entire emulation when another driver
  takes 388. SBPATCH.EXE changes the four FM port entries inside your own
  SBEMUL.SYS to (hopefully) unused ports and fixes the PE checksum, so SBEMUL
  keeps its digital + MIDI and leaves 388 to VOPL3. It:
    - validates the PE checksum before touching anything,
    - locates the FM port table by byte pattern (works across W98 builds),
    - backs up the original as SBEMUL.SYS.orig,
  No Microsoft binary is redistributed - your own file is patched in place.

FILES
  INSTALL.BAT / UNINSTALL.BAT   installer / uninstaller
  INSTALL.REG / UNINSTALL.REG   registry entries
  MIDION.REG                    enables MIDI (applied if you choose FM + MIDI)
  VOPLCFG.REG                   control-panel autostart (applied if chosen)
  SBPATCH.EXE                   the SBEMUL.SYS patcher
  MIDILIST.EXE                  lists MIDI output devices (for FM + MIDI)
  VOPL3.VXD                     OPL3 (+ optional MPU-401 MIDI) port-trap VxD
  VOPLSRV.EXE                   renderer, Nuked OPL3 backend (LGPL 2.1)
  VOPLFAST.EXE                  renderer, Nuked-OPL3-fast backend (LGPL 2.1)
                                (the one you pick is installed as VOPLSRV.EXE)
  VOPLCFG.EXE                   control panel / tray app (see above)
  VOPLSTOP.EXE                  stops the running renderer/control panel
                                (used by INSTALL.BAT on reinstall)
  VOPL3.INI                     renderer settings (FM volume, MIDI device)
