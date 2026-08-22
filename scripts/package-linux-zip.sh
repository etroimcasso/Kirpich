#!/bin/sh
# Package a built Linux executable as the archive a person downloads.
#
#   package-linux-zip.sh <executable> <work-dir> <output.zip> <asset-name>
#
# The archive holds ONE folder, named for the asset, with the executable inside it — so unzipping it
# never scatters files into whatever directory the download landed in.
#
# It must be a real zip, made by zip(1). A release archive has to record the Unix permission bits or
# the executable arrives without its executable bit and the game will not start until the player
# thinks to run chmod. Python's zipfile module and the tar-based alternatives do not record them,
# which is why this refuses to substitute one: an archive that looks fine and does not run is worse
# than a build that stopped.
#
# The archive is then extracted again and the extracted file tested, so what is uploaded is proven
# rather than assumed.

set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: package-linux-zip.sh <executable> <work-dir> <output.zip> <asset-name>" >&2
    exit 2
fi

EXECUTABLE="$1"
WORK="$2"
OUTPUT="$3"
ASSET="$4"

if [ ! -f "$EXECUTABLE" ]; then
    echo "error: no executable at $EXECUTABLE" >&2
    exit 1
fi

if ! command -v zip >/dev/null 2>&1; then
    echo "error: zip is not installed on this runner." >&2
    echo "       Install it (apt-get install zip / dnf install zip) and re-run." >&2
    echo "       It is not substitutable: a release archive must record the Unix permission" >&2
    echo "       bits, and the python and tar alternatives do not." >&2
    exit 1
fi

if ! command -v unzip >/dev/null 2>&1; then
    echo "error: unzip is not installed on this runner; it is what proves the archive is sound" >&2
    echo "       before it is uploaded. Install it (apt-get install unzip) and re-run." >&2
    exit 1
fi

rm -rf "$WORK"
mkdir -p "$WORK/$ASSET"
cp -p "$EXECUTABLE" "$WORK/$ASSET/Kirpich"
chmod 755 "$WORK/$ASSET/Kirpich"

rm -f "$OUTPUT"
# -r from inside the work directory, so the stored paths begin at the asset folder rather than
# carrying the whole absolute path of the machine that built it.
(cd "$WORK" && zip -r -q "$OUTPUT" "$ASSET")

if [ ! -f "$OUTPUT" ]; then
    echo "error: zip reported success but produced no archive at $OUTPUT" >&2
    exit 1
fi

# Extract what a downloader would get, and test that file — not the one we just packed.
rm -rf "$WORK/verify"
mkdir -p "$WORK/verify"
(cd "$WORK/verify" && unzip -q "$OUTPUT")

EXTRACTED="$WORK/verify/$ASSET/Kirpich"
if [ ! -f "$EXTRACTED" ]; then
    echo "error: the archive does not contain $ASSET/Kirpich" >&2
    echo "       it contains:" >&2
    (cd "$WORK/verify" && find . -type f) >&2
    exit 1
fi
if [ ! -x "$EXTRACTED" ]; then
    echo "error: the archive lost the executable bit — a player would have to chmod +x it." >&2
    exit 1
fi

rm -rf "$WORK/verify"
echo "packaged $OUTPUT ($ASSET/Kirpich, executable)"
