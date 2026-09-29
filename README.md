# Mother3Recomp — MOTHER 3, Recompiled

Static recompilation of **MOTHER 3** (Game Boy Advance) to native PC, built on
the [`gbarecomp`](https://github.com/mstan/gbarecomp) framework. Forked from
[EmeraldRecomp](https://github.com/mstan/EmeraldRecomp); the Emerald sources
and variant still in this tree are inherited scaffolding.

Goals: a low-latency native build (battle combos are timed to the music),
a widescreen presentation, and a moddable game through function-level hooks.

## What "static recompilation" means here

The ROM's ARM7TDMI machine code is statically translated to native C — every
function the game runs becomes a generated C function, and the GBA BIOS is
recompiled and executed too. The `gbarecomp` runtime models the rest of the
console: PPU, APU + sound engine, DMA, timers, save chip and I/O.

Any code path the static recompiler missed runs through the built-in
interpreter the first time it's hit, is JIT-compiled to native where possible,
and is cached per ROM in `recomp_cache/<rom-sha1>/`. Misses are also written
to `recomp_master_misses_<code>.toml.frag` for review.

Only symbol metadata from the
[Kurausukun/mother3](https://github.com/Kurausukun/mother3) decompilation
(function names, addresses, sizes) enters the build. The ROM is never
redistributed; supply your own legally-dumped copy.

## Targets

| Target            | Game                          | ROM SHA-1                                  | Debug port |
|-------------------|-------------------------------|--------------------------------------------|------------|
| `Mother3Recomp`   | MOTHER 3 (Japan)              | `4f0f493e12c2a8c61b2d809af03f7abf87a85776` | 19893      |
| `Mother3RecompEN` | MOTHER 3 (English fan patch)  | `306fb8874533b0fd0796208faa8bed2db4ae77fb` | 19894      |
| `Mother3RecompES` | MOTHER 3 (Spanish fan patch)  | `f56db7a401f4796e6f99f08d52707cd0a34665d3` | 19895 |

The runtime refuses to launch on an unrecognized ROM.

ROM locations: `variants/mother3/roms/mother3_jpn.gba`,
`variants/mother3en/roms/mother3_en.gba`, and
`variants/mother3es/roms/mother3_es.gba` (gitignored).

## Controls

Keyboard and controller bindings are in `keybinds.ini`.
Save states: **Shift+F1–F9** save to a slot, **F1–F9** load it.

## Audio latency

Audio output uses a small cushion by default so the music stays close to the
game's timing clock. Tune with environment variables:

| Variable                     | Default | Meaning |
|------------------------------|---------|---------|
| `GBARECOMP_AUDIO_TARGET_MS`  | 32      | Steady-state buffer fill (12–250 ms) |
| `GBARECOMP_AUDIO_SAMPLES`    | 512     | Device callback size in frames (128–4096) |
| `GBARECOMP_AUDIO_PREROLL_MS` | 0       | Extra startup cushion (0 = start at target) |
| `GBARECOMP_AUDIO_MAX_MS`     | target+40 | Latency ceiling; a backlog above it is skipped back to target (0 = off) |
| `GBARECOMP_AUDIO_PROBE`      | off     | `1` logs underruns and buffer fill every 2 s |

If audio crackles, raise `GBARECOMP_AUDIO_TARGET_MS` (try 40–48). The previous
defaults were 60 / 1024 / 250.

## Building from source

**Prerequisites (Windows):** [MSYS2](https://www.msys2.org/) with the mingw64
toolchain, CMake 3.20+, Ninja, and SDL2 (mingw64 package). Run builds from
PowerShell with mingw64 on `PATH`.

```
git clone <this repo> Mother3Recomp
cd Mother3Recomp
git submodule update --init gbarecomp recomp-ui
git -C gbarecomp config submodule.external/arm-recomp-core.url https://github.com/mstan/arm-recomp-core.git
git -C gbarecomp submodule update --init external/arm-recomp-core
cmake -S . -B build -G Ninja
cmake --build build --target Mother3RecompEN     # or Mother3Recomp
```

Regenerating the recompiled C (only after recompiler or config changes):

```
cd variants/mother3en
gba_recompile --rom roms/mother3_en.gba \
    --config game.toml \
    --config symbols/mother3_jpn.toml \
    --symbols symbols/imported_symbols.tsv \
    --data-symbols symbols/imported_data_symbols.tsv \
    --out generated --max-functions 65536
```

`--config` order matters: `game.toml` is the base and wins every conflict
(see `gbarecomp/docs/SYMBOL_OVERLAY.md`).

`gba_recompile` is built from the `gbarecomp` submodule. The generated corpus
is large — expect a multi-minute compile.

For `Mother3RecompES`, place your Spanish patched ROM at
`variants/mother3es/roms/mother3_es.gba`. On Windows, run
`./tools/build.ps1 -Target Mother3RecompES -Regen` from PowerShell.
On Linux, generate the code and build with:

```
cmake --build build --target gba_recompile
(cd gbarecomp && ../build/gbarecomp_build/gba_recompile \
    --bios bios/gba_bios.bin --config bios/gba_bios.toml)
cmake -S . -B build -G Ninja
(cd variants/mother3es && ../../build/gbarecomp_build/gba_recompile \
    --rom roms/mother3_es.gba --config game.toml \
    --config symbols/mother3_jpn.toml \
    --symbols ../mother3en/symbols/imported_symbols.tsv \
    --data-symbols ../mother3en/symbols/imported_data_symbols.tsv \
    --out generated --max-functions 65536)
cmake --build build --target Mother3RecompES
```

The Spanish variant shares JP/EN symbol addresses. A 3,000-frame headless
run reached a Spanish naming screen, with three interpreter-bridged RAM PCs;
full gameplay and static coverage still need validation. ROMs, BIOS files,
and generated code are not part of this repository.

The submodule commands above use the public `mstan/arm-recomp-core` mirror
because the fork's original ARM core URL is unavailable. Git still checks
out the exact core revision pinned by `gbarecomp`.

## License

PolyForm Noncommercial 1.0.0 — see [`LICENSE`](LICENSE). Third-party
components retain their own licenses.

## Legal

This project contains no copyrighted ROM data, no Nintendo BIOS, and no decomp
source — only recompiler/runtime code and symbol metadata. You must supply your
own legally-dumped ROM and BIOS. MOTHER 3 is a trademark of Nintendo / HAL
Laboratory / Shigesato Itoi; this is an unaffiliated, non-commercial
preservation and research project.
