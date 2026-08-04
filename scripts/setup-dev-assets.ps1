# Populate assets/gfx/default/ from the sibling disassembly checkout, for development.
#
# Run once after cloning. This copies ROM-derived graphics into a directory whose contents
# are gitignored and never shipped; it is the developer's equivalent of what the extractor
# does on a player's machine, and it lands the same files in the same place, so the daily
# development run exercises the shipped load path.
#
# Copies only. No conversion, no toolchain, and the ROM is never touched — the source PNGs
# are already in the disassembly.
#
# Keep the source -> destination table below identical to setup-dev-assets.sh.

$ErrorActionPreference = 'Stop'

$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = (Resolve-Path (Join-Path $scriptDir '..')).Path
$tetrisGfx   = Join-Path $projectRoot '..\tetris\gfx'
$destination = Join-Path $projectRoot 'assets\gfx\default'

if (-not (Test-Path -PathType Container $tetrisGfx)) {
    Write-Error @"
the disassembly checkout is not where this script expects it.
       looked for: $tetrisGfx
       it belongs beside this repository, as a sibling directory named 'tetris'.
"@
}

New-Item -ItemType Directory -Force -Path $destination | Out-Null

# source -> destination (destination names match the paths checkRequired() names in src/assets/presence.cpp)
$names = @(
    'configandgameplay.png',
    'font.png',
    'copyrightandtitlescreen.png',
    'multiplayerandburan.png'
)

foreach ($name in $names) {
    $sourceFile = Join-Path $tetrisGfx $name
    if (-not (Test-Path -PathType Leaf $sourceFile)) {
        Write-Error "missing from the disassembly checkout: $sourceFile"
    }
    Copy-Item -Path $sourceFile -Destination (Join-Path $destination $name) -Force
    Write-Host "  $name"
}

Write-Host 'Copied 4 graphics into assets/gfx/default/.'
