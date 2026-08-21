#!/bin/sh
# Build the macOS disk image: the thing a person downloads, opens, and drags into Applications.
#
#     scripts/make-macos-dmg.sh /path/to/Kirpich.app /path/to/output.dmg
#
# A disk image is what a Mac application is distributed as. The alternative — an archive — arrives
# as a folder of files the player has to know what to do with, and loses the executable bit and the
# bundle's symlinks on the way through anything that is not `ditto`.
#
# Nothing here signs or notarizes. A development build has nothing to sign with, and the release
# signs the image after this has produced it, so both paths get an image of exactly the same shape.

set -eu

app=${1:?usage: make-macos-dmg.sh <path to .app> <output .dmg>}
dmg=${2:?usage: make-macos-dmg.sh <path to .app> <output .dmg>}

if [ ! -d "$app" ]; then
    echo "error: no application bundle at $app" >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/kirpich-dmg.XXXXXX")
# The staging tree is ours and is removed whatever happens; the image itself is the output.
trap 'rm -rf "$work"' EXIT INT TERM

# What the window shows when the image is opened: the app, and the folder to drag it into.
cp -R "$app" "$work/$(basename "$app")"
ln -s /Applications "$work/Applications"

mkdir -p "$(dirname "$dmg")"
rm -f "$dmg"

# UDZO is compressed and read-only, which is what a distributed image should be.
hdiutil create -volname "Kirpich" -srcfolder "$work" -ov -format UDZO "$dmg" >/dev/null

echo "Built $dmg"
