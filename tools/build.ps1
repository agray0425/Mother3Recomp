# build.ps1 — one entry point for building Mother3Recomp (humans and agents).
#
#   .\tools\build.ps1                          # incremental build, EN target
#   .\tools\build.ps1 -Target Mother3Recomp    # JP target
#   .\tools\build.ps1 -Regen                   # regenerate recompiled C first
#                                              # (needed after game.toml changes,
#                                              #  e.g. [[code_copy]], [[mod_function_hook]])
#
# Puts MSYS2 mingw64 on PATH (cc1plus/windres fail silently without its DLLs)
# and repairs TEMP if it points somewhere unwritable (e.g. C:\Windows).
param(
    [ValidateSet("Mother3RecompEN", "Mother3Recomp", "Mother3RecompES")]
    [string]$Target = "Mother3RecompEN",
    [switch]$Regen,
    [string]$Mingw = "C:\msys64\mingw64\bin"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not ($env:Path -split ";" | Where-Object { $_ -ieq $Mingw })) {
    $env:Path = "$Mingw;" + $env:Path
}
if (-not $env:TEMP -or $env:TEMP -ieq "C:\Windows" -or -not (Test-Path $env:TEMP)) {
    $env:TEMP = Join-Path $env:LOCALAPPDATA "Temp"
    $env:TMP = $env:TEMP
}

if (-not (Test-Path "build\build.ninja")) {
    cmake -S . -B build -G Ninja
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

if ($Regen) {
    $v = @{ "Mother3RecompES" = @("mother3es", "roms\mother3_es.gba");
            "Mother3RecompEN" = @("mother3en", "roms\mother3_en.gba");
            "Mother3Recomp"   = @("mother3",   "roms\mother3_jpn.gba") }[$Target]
    cmake --build build --target gba_recompile
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $recomp = Join-Path $root "build\gbarecomp_build\gba_recompile.exe"
    if (-not (Test-Path "gbarecomp\src\runtime\generated_bios\bios_recompiled.cpp")) {
        Push-Location (Join-Path $root "gbarecomp")
        try {
            & $recomp --bios bios\gba_bios.bin --config bios\gba_bios.toml
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
        } finally { Pop-Location }
        cmake -S . -B build -G Ninja
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    $symbolDir = if ($Target -eq "Mother3RecompES") { "..\mother3en\symbols" } else { "symbols" }
    Push-Location (Join-Path $root "variants\$($v[0])")
    try {
        & $recomp --rom $v[1] `
            --config game.toml --config symbols\mother3_jpn.toml `
            --symbols "$symbolDir\imported_symbols.tsv" `
            --data-symbols "$symbolDir\imported_data_symbols.tsv" `
            --out generated --max-functions 65536
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    } finally { Pop-Location }
}

cmake --build build --target $Target
exit $LASTEXITCODE
