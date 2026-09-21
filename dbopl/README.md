# DBOPL — DOSBox's OPL2/OPL3 emulator, as a VOPL3 renderer backend

The third renderer backend (`VOPLDB.EXE`): far lighter on the CPU than the
Nuked cores, and less exact. Where Nuked emulates the chip cycle by cycle at its
native 49716 Hz and resamples, DBOPL computes directly at the output rate and
only for the voices that are sounding.

## Origin

`dbopl.cpp` / `dbopl.h` come from **DOSBox SVN trunk r4494**
(`src/hardware/`, last changed in r4468), © The DOSBox Team, with the small
Open Watcom patch below applied.

## License

**GPL v2 or later** (see [COPYING](COPYING), DOSBox's own copy). A renderer
build containing it — `VOPLDB.EXE` — is therefore distributed under the GPL as
a whole. VOPL3's other programs, and its own source files (including the glue
and stand-in headers here), are unaffected and stay MIT.

## The Open Watcom patch ([dbopl-openwatcom.patch](dbopl-openwatcom.patch))

Open Watcom C++ cannot take the address of a member-function-template
specialization (`&Channel::BlockTemplate< sm2FM >` — error E512), which DBOPL
does to fill its dispatch pointers. The patch adds a plain, non-template
wrapper member per specialization (15) and points the 16 address-of sites at
them. The synthesis code itself is unchanged. To update from a newer DOSBox,
re-apply the same change.

## VOPL3's files here

- `dosbox.h`, `adlib.h` — minimal stand-ins for the two DOSBox headers
  `dbopl.cpp`/`dbopl.h` include (typedefs, a few macros, and an unused mixer
  interface), so those files build outside DOSBox.
- `dbopl_glue.cpp` / `dbopl_glue.h` — a C interface for the renderer, driving
  `DBOPL::Chip` the way DOSBox's own `Handler` does: `InitTables()` before
  `Setup()`, mono output in OPL2 mode and stereo in OPL3 mode, at most 512
  samples per call, and DOSBox's address mapping for the second register bank.
  Plus one addition: register writes are **paced** at least ~40 µs apart, as
  Nuked's buffered writes (and a real chip on the ISA bus) space them. Without
  it, some music comes out with a loud buzz lasting up to half a second,
  always at the same spot: a register sequence that freezes voices partway
  through their release leaves them far too loud when there is no gap between
  its writes.
