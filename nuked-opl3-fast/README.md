# Nuked-OPL3-fast

Bit-exact, performance-optimized fork of
[Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3), Nuke.YKT's
cycle-accurate Yamaha YMF262 (OPL3) emulator. Pinned to upstream commit
`cfedb09` (Nuked-OPL3 v1.8).

Audio output is identical to upstream for the same register stream.

## Performance

Measured on x86-64 Linux, GCC `-O2`, best of repeated runs:

| Workload                                | Upstream | This fork | Speedup |
|-----------------------------------------|----------|-----------|---------|
| Synthetic chip-core (ns/sample)         | ~310     | ~140      | ~2.2x   |
| Light IMF tune (ns/frame)               | ~295     | ~145      | ~2.0x   |
| Dense full-track VGM render (3.5 min)   | 4.3 s    | 2.7 s     | ~1.6x   |

Mileage may vary depending on content being rendered. There are several
shortcuts that are only hit for channels that are silent, uninitialized, etc.
Tests are based on a light early-Apogee-style IMF tune and a recent demoscene
production that is doing stuff in basically all the slots all the time.

Across a 40-track random sample from
[The OPL Archive](https://opl.wafflenet.com/), this fork rendered every file
1.42x to 2.30x faster than upstream (median 1.99x), with output identical to
upstream Nuked-OPL3 on all 40.

## API

Drop-in source-level replacement. All public functions and the public
`opl3_chip` struct are unchanged.

Internal structs (`opl3_slot`, `opl3_channel`) gained some cached fields.
If your code touches these directly, you'll need to change it.

## Building

Add to your build:

- `opl3.c`
- `opl3.h`
- `wf_rom.h`

`gen_logsin.py` is the generator for `wf_rom.h`. The generated file is
committed; you only need Python if you want to regenerate it.

If it has been a while since you have updated your project's vendored copy of
Nuked-OPL3, you should be aware of a couple things:

- You may pick up some fixes from upstream that you didn't have before. Many
  projects use a version of Nuked-OPL3 that is missing some fixes to the
  envelope algorithm. If your audio output isn't sample-for-sample the same
  after switching to Nuked-OPL3-fast, that's why, and your emulation is now more
  accurate. Before filing an issue claiming a divergence from upstream
  Nuked-OPL3, make sure you're comparing to *current* upstream.
- Many projects have their own patches on top of Nuked-OPL3, commonly to
  add/modify pan laws, mixer-level muting of channels, buffering tweaks, etc. Be
  sure you reapply any such project-local patches to Nuked-OPL3-fast.

## Bit-exactness

Nuked-OPL3-fast produces identical output to an unmodified upstream build.

Among other extensive verifications, a 40-track random sample from
[The OPL Archive](https://opl.wafflenet.com/) produced render checksums
identical to upstream Nuked-OPL3 on every file.

[CBMC](https://www.cprover.org/) was used to verify the correctness of key
paths.

The test suite will be made available in the near future.

## Changes

The full annotated list is at the top of `opl3.c`. At a high level:

- Waveform math replaced by a precomputed 8x1024 logsin lookup table
  (`wf_rom.h`).
- Several write-time caches on `opl3_slot` (TL+KSL sum, envelope key-scale
  shift, non-vibrato phase increment, envelope rate resolution).
- Fast paths in `OPL3_ProcessSlot` for the common attenuated-and-key-off,
  permanently-dead, and sustain-with-rate-zero slot states.
- Silent-regime variant of `OPL3_SlotGenerate` used by the attenuated
  fast path.
- Both 18-channel mix loops in `OPL3_Generate4Ch` unrolled to skip dummy
  reads for muted and 2-op voices via a per-channel `out_cnt`.
- Rhythm-mode dispatch in `OPL3_PhaseGenerate` fused into a single
  `slot_num` switch for jump-table dispatch.
- Hot fields hoisted into the first cache line of `opl3_slot`; struct
  size dropped from 96 to 88 bytes.
- Per-slot cache of the phase increment at each vibrato position
  (`pg_inc_vib`), rebuilt on the writes that affect it.
- Noise LFSR hoisted out of slot processing into a word-parallel
  36-step advance at the top of `OPL3_Generate4Ch`.
- All 36 slots processed as channel pairs before either mix pass, with
  per-channel pointer lists reproducing the L/R sample-delay quirk and an
  inlined skip for trivially-silent slots.

## Why a fork and not a PR?

These changes were offered to upstream, but declined. My interpretation is that
Nuke.YKT's emulators are intended to serve partly as something of a readable
specification of the behavior of the real chip, and some of my optimizations
conflict with that. I don't think upstream necessarily *missed* optimizations so
much as made an informed choice to forego some. Much respect is owed to Nuke.YKT
for creating and maintaining many gold-standard sound chip emulators.

## License

LGPL-2.1-or-later. See `LICENSE`.

## Credits

See comments in `opl3.c`.

Nuked-OPL3 was created by Nuke.YKT.

This fork is maintained by Tony Gies.
