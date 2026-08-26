#!/bin/sh
# Assert that a package is safe to distribute: no ROM-derived content, and no binary that would
# look for its files on the machine that built it.
#
# Three things are checked.
#
# 1. THE ASSETS. Kirpich's graphics and its sound driver image both come from a copyrighted ROM
#    and may never be distributed. A packaged artifact carries the asset directories EMPTY,
#    structure only — the .gitkeep placeholder and nothing else. The whole assets/ subtree is
#    swept, not a list of named directories, so a directory added later is covered without this
#    script having to be taught about it.
#
# 2. THE ROM ITSELF, by identity, anywhere in the tree. This is the check that does not depend on
#    guessing where a file landed. The build copies a LoadFromPath asset beside the binary at its
#    logical path, so a machine with a ROM in its source tree has one in its build output; if a
#    staging step ever picked that up, a directory-shaped check would catch it only in the
#    directories it was told about. This one recognises the file by what it IS — exact size, then
#    SHA-1 — wherever it sits.
#
# 3. THE BINARY. A build configured with KIRPICH_DEV_ASSET_ROOT=ON bakes its own source tree's
#    path into the executable, and a binary carrying that path resolves assets against a
#    directory that exists on one machine in the world. A distributable is configured with
#    -DKIRPICH_DEV_ASSET_ROOT=OFF, and this verifies the result rather than trusting the flag was
#    passed: it reads the paths out of the binary and refuses any that lead to a Kirpich source
#    tree. That is a check the build cannot do for itself and the test suite cannot see.
#
# Run it against a packaging staging tree by passing that tree's root:
#
#     scripts/check-distributable-clean.sh /path/to/staged/package
#
# The binary is found inside that tree, or named outright as the second argument:
#
#     scripts/check-distributable-clean.sh /path/to/staged/package /path/to/Kirpich.app
#
# With no argument it checks this working tree, which is expected to FAIL for a developer
# who has run setup-dev-assets — that is the check proving it can tell the difference.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${1:-$(CDPATH= cd -- "$script_dir/.." && pwd)}
binary=${2:-}

status=0

for directory in "$root/assets/gfx/default" "$root/assets/audio/default"; do
    if [ ! -d "$directory" ]; then
        echo "error: expected asset directory is absent: $directory" >&2
        echo "       the structure ships even when the content does not." >&2
        status=1
    fi
done

# Anything anywhere under assets/ that is not a placeholder is content that must not ship. Swept as
# one subtree rather than per named directory: the point is that no asset content ships, and a check
# that only looks where it was told to look answers a narrower question than that.
if [ -d "$root/assets" ]; then
    offenders=$(find "$root/assets" -type f ! -name .gitkeep)
    if [ -n "$offenders" ]; then
        echo "error: ROM-derived content present under $root/assets:" >&2
        echo "$offenders" | sed 's/^/       /' >&2
        status=1
    fi
fi

# ── The ROM itself, wherever it is ────────────────────────────────────────────
# Source of truth for both values: kRomSize / kRomSha1 in src/assets/extract.cpp. They are repeated
# here because a shell script cannot read a C++ constant; the pairing is what the planted-ROM check
# in the plan's verification exercises.
rom_size=32768
rom_sha1=74591cc9501af93873f9a5d3eb12da12c0723bbc

# Size first, so nothing else in the tree is ever hashed.
candidates=$(find "$root" -type f -size "${rom_size}c")

if [ -n "$candidates" ]; then
    # Unquoted on use: "shasum -a 1" has to split into a command and its argument.
    hash_tool=
    if command -v sha1sum >/dev/null 2>&1; then
        hash_tool=sha1sum
    elif command -v shasum >/dev/null 2>&1; then
        hash_tool="shasum -a 1"
    fi

    if [ -z "$hash_tool" ]; then
        # Loud rather than skipped, the same posture as the missing-'strings' case below: a file the
        # right size to be the ROM went unidentified, so this package has not been checked.
        echo "error: files of the ROM's exact size are present and no SHA-1 tool is available," >&2
        echo "       so they cannot be identified. Install coreutils (sha1sum) or perl (shasum)." >&2
        echo "$candidates" | sed 's/^/       /' >&2
        status=1
    else
        # Collected by substitution rather than acted on inside the loop: a `while` in a pipeline
        # runs in a subshell, so a status set there would not survive it.
        planted=$(echo "$candidates" | while IFS= read -r candidate; do
            [ -n "$candidate" ] || continue
            if [ "$($hash_tool "$candidate" | awk '{print $1}')" = "$rom_sha1" ]; then
                echo "$candidate"
            fi
        done)

        if [ -n "$planted" ]; then
            echo "error: the Game Boy Tetris ROM is present in $root:" >&2
            echo "$planted" | sed 's/^/       /' >&2
            echo "       it is the player's own file and must never be distributed." >&2
            status=1
        fi
    fi
fi

# ── The binary ────────────────────────────────────────────────────────────────
# Named outright, or looked for where a package puts it: loose in the tree, or inside a bundle.
if [ -z "$binary" ]; then
    for candidate in \
        "$root/Kirpich" \
        "$root/Kirpich.exe" \
        "$root/Kirpich.app/Contents/MacOS/Kirpich" \
        "$root/build/Kirpich" \
        "$root/build/Kirpich.app/Contents/MacOS/Kirpich"
    do
        if [ -f "$candidate" ]; then
            binary=$candidate
            break
        fi
    done
fi

# A bundle may be handed over directly; the executable inside it is what carries the paths.
if [ -d "$binary" ] && [ -f "$binary/Contents/MacOS/Kirpich" ]; then
    binary=$binary/Contents/MacOS/Kirpich
fi

if [ -z "$binary" ] || [ ! -f "$binary" ]; then
    # Said out loud rather than passed over: a package whose binary was never checked has not
    # been checked, and this script's whole job is to be the thing that noticed.
    echo "error: no binary to check under $root" >&2
    echo "       pass one as the second argument, or stage it before running this." >&2
    status=1
elif ! command -v strings >/dev/null 2>&1; then
    echo "error: 'strings' is not available, so the binary cannot be checked." >&2
    echo "       install binutils (Linux) or the Xcode command line tools (macOS)." >&2
    status=1
else
    # A source tree is recognised by what only a source tree has. Any absolute path in the binary
    # that leads to one is a development asset root, whichever checkout it came from.
    # The leading-slash test is spelled with a prefix strip rather than a `case` glob: bash 3.2,
    # which is what /bin/sh is on macOS, mis-parses a case pattern's `)` inside $( ) as the
    # closing paren of the substitution itself.
    baked=$(strings "$binary" | while IFS= read -r candidate; do
        if [ "${candidate#/}" != "$candidate" ] &&
           [ -f "$candidate/src/main.cpp" ] &&
           [ -f "$candidate/CMakeLists.txt" ]; then
            echo "$candidate"
        fi
    done)

    if [ -n "$baked" ]; then
        echo "error: $binary carries a development asset root:" >&2
        echo "$baked" | sed 's/^/       /' >&2
        echo "       it would read assets from that directory instead of the player's own." >&2
        echo "       configure the distributable with -DKIRPICH_DEV_ASSET_ROOT=OFF." >&2
        status=1
    else
        echo "Binary carries no development asset root: $binary"
    fi
fi

if [ "$status" -eq 0 ]; then
    echo "Asset directories are empty of content. Safe to package."
else
    echo "" >&2
    echo "Refusing to call this package clean." >&2
fi

exit "$status"
