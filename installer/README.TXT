VOPL3 - OPL3 FM emulation for Windows 98/ME
===========================================

For Windows 98/ME. Adds OPL3 FM synthesis (Nuked OPL3, DOSBox's DBOPL or
ymfm) to DOS boxes,
coexisting with Microsoft's SBEMUL - the standard Win98 DOS Sound Blaster
emulation, which gives digital audio + MIDI but NO FM. The OPL3 audio plays
through the normal Windows audio output (waveOut), so its output is not tied
to any particular sound card.

VOPL3 is meant for systems whose audio hardware has no FM synthesizer of its
own and rely on SBEMUL. If your sound card already provides hardware OPL3/FM
(e.g. some Yamaha YMF7xx cards), DOS FM already works through the card and its
driver usually owns ports 388-38B itself - in that case you do NOT need VOPL3.
If you still want VOPL3's MIDI routing on such a card, choose FM = Left free
(see CHOICES).

INSTALL
  Run INSTALL.BAT from Windows (in an MS-DOS Prompt / DOS box), from the folder
  that contains these files. It:
    - installs VOPL3.VXD (C:\WINDOWS\SYSTEM, loads at boot),
    - installs the renderer (C:\VOPL3\VOPLSRV.EXE, autostarts at login).
      If VOPL3 plays FM, you choose between four builds:
        1 = Nuked OPL3 (reference, the most accurate),
        2 = Nuked OPL3 fast (same output as 1, about half the CPU use -
            recommended, especially on slower machines),
        3 = DOSBox DBOPL (DOSBox's emulator: a small fraction of the CPU
            use of 1 or 2, but less accurate),
        4 = ymfm (MAME's emulator: CPU use between 2 and 1),
    - installs the control panel (C:\VOPL3\VOPLCFG.EXE; you choose whether
      it starts with Windows, minimized to the system tray),
    - asks who handles the FM ports and the MIDI ports (see CHOICES below),
    - sets up SBEMUL so it leaves the chosen ports alone (keeps its
      digital sound): FM through SBEMUL's own SoftFM registry setting, MIDI
      by patching SBEMUL.SYS (see ABOUT THE SBEMUL PATCH).
  Then REBOOT.

  If it says SBEMUL.SYS is "in use", finish that one step from MS-DOS mode
  (Start > Shut Down > Restart in MS-DOS mode), change to this folder and run:
      SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS <groups>
  where <groups> is what the installer printed: fm, midi, or fm midi
  (SBPATCH.EXE ... none gives back everything SBPATCH had moved).

USE
  After reboot the renderer starts automatically. In your DOS game set
  Sound FX = Sound Blaster, and Music = AdLib (or OPL3) or General MIDI,
  depending on what you chose. You then get the music AND digital sound
  effects (via SBEMUL) at the same time.

CHOICES (asked by INSTALL.BAT)
  FM - the AdLib/OPL3 ports 388-38B and the Sound Blaster FM ports 2x8/2x9
  (base+8/+9, see ABOUT THE SBEMUL PATCH):
    VOPL3 (recommended)  SBEMUL is steered away from these ports, and VOPL3
                         traps them and synthesizes the music.
    Left free            SBEMUL is steered away from these ports, and VOPL3
                         does not trap them either: nothing claims them, so
                         they are free for anything else that uses them.
    SBEMUL               left as stock: games detect an AdLib, but AdLib
                         music is silent.
  MIDI - the MPU-401 ports 330/331:
    VOPL3 (recommended)  SBEMUL is steered away from these ports, and VOPL3
                         routes DOS-game MIDI to any device (see MIDI below).
    SBEMUL               left as stock: MIDI plays on the Microsoft GS
                         Wavetable synth.
  Leaving both to SBEMUL is not offered - VOPL3 would have nothing to do.
  To change your choices later, just run INSTALL.BAT again.

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

TUNING (advanced - C:\VOPL3\VOPL3.INI, see the comments in the file)
  buffer=<ms>     audio buffering depth, default 160 (range 40-960). Raise
                  it (e.g. 480) if FM audio stutters in a regular pattern on
                  your sound driver; costs some FM latency. Applied at
                  renderer start.
  rate=<hz>       output sample rate: 11025, 16000, 22050, 32000, 44100
                  or 48000 (default; any other value counts as 48000). With
                  the Nuked and ymfm builds the OPL3 engine always runs at
                  the chip's native 49716 Hz and is resampled to this rate,
                  so its CPU use is the same at any rate (DBOPL computes
                  directly at this rate). 44100 can spare Windows a
                  sample-rate conversion on 44.1 kHz-native hardware.
                  Applied at renderer start; falls back to 48000 if the
                  driver refuses it.
  priority=...    auto (default) = realtime only while producing audio;
                  realtime = hold realtime always (pre-A04 behaviour);
                  normal = never raise (diagnostic).
  idleclose=<s>   release the sound device after this many seconds of FM
                  silence instead of streaming silence; 0 = never (default),
                  otherwise 5-3600. Taken back automatically when a game
                  plays FM again. Leave at 0 unless you have a reason (e.g.
                  data corruption during file transfers between drives on
                  some VIA chipsets). Applies live.

MIDI (if you chose MIDI = VOPL3 at install)
  Without VOPL3, DOS-game MIDI plays through SBEMUL's fixed synth (Microsoft
  GS Wavetable). With MIDI = VOPL3, VOPL3 takes over MPU-401 MIDI and sends
  it through the Windows MIDI Mapper - by default the same GS synth, so
  nothing changes until you pick another device. It can play on ANY
  installed synth or device - a software synth (Roland VSC, Yamaha S-YXG50,
  ...), a hardware wavetable, or an external module on a MIDI interface.
    - Pick the target in the VOPL3 control panel (VOPLCFG.EXE - easiest,
      applies live), in Control Panel > Multimedia > MIDI, or in
      C:\VOPL3\VOPL3.INI  [midi] device=  (65535 = MIDI Mapper (default), or a
      device index). Run MIDILIST.EXE to list devices and their indices.
    - This replaces SBEMUL's MIDI; with MIDI = SBEMUL it is left untouched.
    - Only UART-mode MPU-401 is supported.

UNINSTALL
  Run UNINSTALL.BAT (restores the original SBEMUL.SYS, removes VOPL3), reboot.

ABOUT THE SBEMUL PATCH
  SBEMUL claims its FM ports only to fake AdLib *detection* - it makes no FM
  sound. These are the AdLib ports 388-38B and the Sound Blaster's own FM
  ports at base+8/+9, where "base" is the Sound Blaster's I/O address (the A
  value in BLASTER, normally 220, so 228/229). And it tears down its entire
  emulation when another driver takes 388 (or the MIDI ports).
  FM: SBEMUL has its own registry setting for this, SoftFM (in
  HKLM\Software\Microsoft\Multimedia\WDMAudio\SBEmulator). With SoftFM=1
  it leaves the AdLib ports 388-38B and the Sound Blaster's FM ports at
  base+8/+9 (228/229 at base 220) alone, and keeps its digital sound.
  INSTALL.BAT sets it when FM = VOPL3 or left free, and removes it when
  FM = SBEMUL. With FM = VOPL3, VOPL3 then traps both groups - base+8/+9
  for every SB base (220/240/260/280), so it needs no base setting - as a
  real Sound Blaster answers with the same chip at both. Some games (e.g.
  those using the DiamondWare Sound ToolKit) look for the FM chip at
  base+8 first and only fall back to 388 if nothing answers.
  MIDI: SBPATCH.EXE changes the MPU-401 port entries (330/331) inside your
  own SBEMUL.SYS to unused ports and fixes the PE checksum, so SBEMUL keeps
  its digital sound and leaves the MIDI ports alone. SoftFM is known to work
  on SBEMUL 4.10.2222 (98SE) and 4.10.2223 (the Q269601 hotfix); on any
  other SBEMUL build SBPATCH also moves the FM port entries (388-38B), as a
  fallback in case that build doesn't read SoftFM. SBPATCH:
    - locates each port table by byte pattern, required to match exactly
      once (works across W98 builds, refuses unrecognised files),
    - gives ports back to SBEMUL when you reinstall with different choices
      (e.g. MIDI = VOPL3 reinstalled as MIDI = SBEMUL), and gives back the
      FM ports an earlier VOPL3 version had moved where SoftFM now does it,
    - tolerates files modified by other patches (e.g. the SB16-enable
      patch): a stale PE checksum only warns, and the patched file gets a
      correct checksum written back,
    - backs up the original as SBEMUL.SYS.orig,
  No Microsoft binary is redistributed - your own file is patched in place.

FILES
  INSTALL.BAT / UNINSTALL.BAT   installer / uninstaller
  INSTALL.REG / UNINSTALL.REG   registry entries (incl. SBEMUL's SoftFM)
  MIDION.REG                    MIDI = VOPL3      (applied if chosen)
  FMSBEMUL.REG                  FM = SBEMUL, SoftFM removed (applied if chosen)
  FMFREE.REG                    FM = left free    (applied if chosen)
  VOPLCFG.REG                   control-panel autostart (applied if chosen)
  SBPATCH.EXE                   the SBEMUL.SYS patcher
  MIDILIST.EXE                  lists MIDI output devices (for MIDI = VOPL3)
  VOPL3.VXD                     OPL3 (+ optional MPU-401 MIDI) port-trap VxD
  VOPLSRV.EXE                   renderer, Nuked OPL3 backend (LGPL 2.1)
  VOPLFAST.EXE                  renderer, Nuked-OPL3-fast backend (LGPL 2.1)
  VOPLDB.EXE                    renderer, DOSBox DBOPL backend (GPL v2 or
                                later - see DBOPL-LICENSE.txt; its source is
                                with VOPL3's, at the project's GitHub page)
  VOPLYM.EXE                    renderer, ymfm backend (BSD 3-clause - see
                                YMFM-LICENSE.txt)
                                (the one you pick is installed as VOPLSRV.EXE)
  VOPLCFG.EXE                   control panel / tray app (see above)
  VOPLSTOP.EXE                  stops the running renderer/control panel
                                (used by INSTALL.BAT on reinstall)
  VOPL3.INI                     renderer settings (FM volume, MIDI device)
  DBOPL-LICENSE.txt             the GNU GPL v2, which covers VOPLDB.EXE
  YMFM-LICENSE.txt              ymfm's license (BSD 3-clause), for VOPLYM.EXE
