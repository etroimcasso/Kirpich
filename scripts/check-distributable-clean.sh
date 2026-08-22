#!/bin/sh
# Assert that a package is safe to distribute: no ROM-derived content, and no binary that would
# look for its files on the machine that built it.
#
# Two things are checked.
#
# 1. THE ASSETS. Kirpich's graphics and its sound driver image both come from a copyrighted ROM
#    and may never be distributed. A packaged artifact carries the asset directories EMPTY,
#    structure only — the .gitkeep placeholder and nothing else.
#
# 2. THE BINARY. A build configured with KIRPICH_DEV_ASSET_ROOT=ON bakes its own source tree's
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
        continue
    fi

    # Anything that is not the .gitkeep placeholder is content that must not ship.
    offenders=$(find "$directory" -mindepth 1 ! -name .gitkeep)
    if [ -n "$offenders" ]; then
        echo "error: ROM-derived content present in $directory:" >&2
        echo "$offenders" | sed 's/^/       /' >&2
        status=1
    fi
done

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
