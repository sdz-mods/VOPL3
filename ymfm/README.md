# ymfm — MAME's OPL3 emulator, as a VOPL3 renderer backend

The fourth renderer backend (`VOPLYM.EXE`). ymfm is Aaron Giles' collection of
Yamaha FM cores used by MAME; VOPL3 builds only its OPL3 (YMF262). Like Nuked
OPL3, it runs the chip at its native 49716 Hz and resamples; its CPU cost is
about that of Nuked-OPL3-fast (lower with few voices playing).

## Origin

`ymfm.h`, `ymfm_fm.h`, `ymfm_fm.ipp`, `ymfm_opl.h` and `ymfm_opl.cpp` come from
**https://github.com/aaronsgiles/ymfm**, commit **81aec25** (2026-07-27),
© Aaron Giles, with the Open Watcom patch below applied.

## License

**BSD 3-clause** (see [LICENSE](LICENSE), ymfm's own copy). Its notice ships
alongside `VOPLYM.EXE` as `YMFM-LICENSE.txt`, as the license asks for binary
distributions. VOPL3's own files here (the glue and the compatibility header)
are MIT, like the rest of VOPL3.

## The Open Watcom patch ([ymfm-openwatcom.patch](ymfm-openwatcom.patch))

ymfm is written in C++14; Open Watcom C++ predates C++11. The patch changes no
synthesis code, only how it is spelled:

- `ymfm.h` includes `ymfm_owcompat.h` first, which maps `constexpr` to
  `const`, drops `override`, defines `nullptr` as 0, and includes `<stdint.h>`
  (Watcom's `<cstdint>` puts the types in `std::` only);
- `using x = y;` aliases become `typedef`s; enums lose their `: uint32_t`
  underlying type; `= default` destructors become `{ }`;
- the variadic-template logging helpers become C varargs functions (all
  logging is compiled out anyway);
- `std::unique_ptr` / `std::make_unique` for the channels and operators become
  plain pointers and `new`; `std::array` becomes a plain array; brace
  initializers and an in-class member initializer move into constructors;
  two range-`for` loops and one `auto` are spelled out;
- the friend declaration of `fm_engine_base` (a class template declared later)
  is dropped and `ymfm_interface`'s protected members are made public instead;
- save states (never used by VOPL3) are a no-op, so their typed overloads are
  not instantiated;
- every chip except the YMF262 is wrapped in `#if 0` (along with the ADPCM and
  PCM headers only those need), rather than deleted, to keep the patch small.

## VOPL3's files here

- `ymfm_owcompat.h` — the compatibility header described above.
- `ymfm_glue.cpp` / `ymfm_glue.h` — a C interface for the renderer, built as
  one unit with ymfm (it includes `ymfm_opl.cpp`, so a compiler that inlines
  ymfm's OPL3 engine templates still leaves the glue a copy to call), driving
  `ymfm::ymf262` at its native rate (clock 14.31818 MHz / 288) and linearly
  interpolating to the output rate. Outputs A+C go left and B+D right, as
  Nuked mixes them, halved to Nuked's level so the FM volume setting means the
  same with every backend. ymfm's own `generate` clamps its mix to 16 bits at
  its level, twice Nuked's, which after halving would clip at half the
  loudness Nuked and DBOPL clip at; so the glue takes the mix before that
  clamp (a small `ymf262` subclass), halves it, and only then clamps - the
  same headroom as the other backends. Register writes are **paced** at least
  2 chip samples (~40 µs) apart, as in the DBOPL glue (see
  [../dbopl/README.md](../dbopl/README.md)): ymfm also applies every write the
  moment it gets it, and without the gaps some music loses notes (on the
  capture that showed the DBOPL buzz, unpaced ymfm came out at under half of
  Nuked's level; paced, within 2% of it). No static objects with
  constructors: the renderer's startup code is Watcom's, which doesn't run a
  GCC-built file's static constructors.
