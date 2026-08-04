#!/bin/sh
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
# Keep the source -> destination table below identical to setup-dev-assets.ps1.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
tetris_gfx="$project_root/../tetris/gfx"
destination="$project_root/assets/gfx/default"

if [ ! -d "$tetris_gfx" ]; then
    echo "error: the disassembly checkout is not where this script expects it." >&2
    echo "       looked for: $tetris_gfx" >&2
    echo "       it belongs beside this repository, as a sibling directory named 'tetris'." >&2
    exit 1
fi

mkdir -p "$destination"

# source -> destination (destination names match the logical paths in src/assets/presence.h)
for name in \
    configandgameplay.png \
    font.png \
    copyrightandtitlescreen.png \
    multiplayerandburan.png
do
    source_file="$tetris_gfx/$name"
    if [ ! -f "$source_file" ]; then
        echo "error: missing from the disassembly checkout: $source_file" >&2
        exit 1
    fi
    cp -- "$source_file" "$destination/$name"
    echo "  $name"
done

echo "Copied 4 graphics into assets/gfx/default/."
