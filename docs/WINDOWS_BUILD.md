# Building Mother3Recomp on Windows

Step-by-step notes for building the Japanese target (`Mother3Recomp`) on Windows
with MSYS2. Tested with MSYS2 mingw64 (GCC 16.2.0), 7 GB RAM.

You must supply your own legally obtained ROM and GBA BIOS. Never commit them,
and never commit generated code (`variants/*/generated/`, `gbarecomp/src/runtime/generated_bios/`):
it is derived from them.

## 1. Tools

1. Install [MSYS2](https://www.msys2.org/). The toolchain needs several GB, so pick
   a drive with space (e.g. `D:\msys64`).
2. In the **MSYS2 MINGW64** shell:
   ```
   pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 git
   ```
3. Add `<msys>\mingw64\bin` to your Windows user `PATH`, then reopen PowerShell:
   ```
   $old = [Environment]::GetEnvironmentVariable("Path", "User")
   [Environment]::SetEnvironmentVariable("Path", "$old;D:\msys64\mingw64\bin", "User")
   ```
4. Check: `gcc --version`, `cmake --version`, `ninja --version`.

## 2. Get the sources

```
git clone --recurse-submodules https://github.com/agray0425/Mother3Recomp
cd Mother3Recomp
```

If the clone fails with `repository 'https://github.com/<you>/arm-recomp-core.git' not found`,
the nested submodule URL is relative and does not resolve for forks. Point it at
the upstream repository and update again:
```
git -C gbarecomp config submodule.external/arm-recomp-core.url https://github.com/mstan/arm-recomp-core.git
git submodule update --init --recursive
```

## 3. Place your files

| File | Location | Check |
|---|---|---|
| MOTHER 3 (Japan) ROM | `variants\mother3\roms\mother3_jpn.gba` | SHA-1 `4f0f493e12c2a8c61b2d809af03f7abf87a85776` |
| GBA BIOS | `gbarecomp\bios\gba_bios.bin` | 16384 bytes |

```
Get-FileHash -Algorithm SHA1 variants\mother3\roms\mother3_jpn.gba
(Get-Item gbarecomp\bios\gba_bios.bin).Length
```

## 4. Configure and build the recompiler

```
cmake -S . -B build -G Ninja
cmake --build build --target gba_recompile
```
The tool is `build\gbarecomp_build\gba_recompile.exe`. Compiler warnings
(`initialized and declared 'extern'`) are expected.

## 5. Recompile the BIOS, then the game

```
build\gbarecomp_build\gba_recompile.exe --bios gbarecomp\bios\gba_bios.bin --config gbarecomp\bios\gba_bios.toml --out gbarecomp\src\runtime\generated_bios

cd variants\mother3
..\..\build\gbarecomp_build\gba_recompile.exe --rom roms\mother3_jpn.gba --config game.toml --config symbols\mother3_jpn.toml --symbols symbols\imported_symbols.tsv --data-symbols symbols\imported_data_symbols.tsv --out generated --max-functions 65536
cd ..\..
```
Keep `game.toml` first: it wins on conflicts.

The BIOS step overwrites the tracked file `bios_dispatch_table.cpp` with BIOS-derived
code. To avoid committing it by accident:
```
git -C gbarecomp update-index --skip-worktree src/runtime/generated_bios/bios_dispatch_table.cpp
```

## 6. Build the game

```
cmake -S . -B build -G Ninja
cmake --build build --target Mother3Recomp -j 2
```
This takes many minutes and prints thousands of warnings; look for `FAILED:` only.
To keep the log small: `... 2>&1 | Out-File build.log` then
`Select-String build.log -Pattern "FAILED|error:|undefined reference"`.

With little RAM use `-j 1` or `-j 2`; high parallelism can exhaust memory and push
Windows into swapping on the system drive.

The executable is `build\Mother3Recomp.exe`. Run it with `--help` for options
(`--bios`, `--rom`).

## Troubleshooting

- `cmake` / `gcc` not found: wrong shell or `PATH` not updated (reopen PowerShell).
- `ArmRecompCore.cmake` not found: nested submodule missing, see section 2.
- `undefined reference to emerald::...` or `gf_ReadFlash1` at link time: your
  `src/main.cpp` predates the fix that gates the Emerald-only code.
- `BIOS recompiled output absent` during configure: run the BIOS step in section 5,
  then re-run `cmake -S . -B build -G Ninja`.
