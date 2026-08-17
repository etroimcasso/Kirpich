#!/bin/sh
# Assert that no ROM-derived content is present in the asset directories.
#
# Kirpich's graphics and its sound driver image both come from a copyrighted ROM and may never
# be distributed. A packaged artifact must therefore carry the asset directories EMPTY,
# structure only — the .gitkeep placeholder and nothing else. This exits non-zero the moment
# anything else is in there.
#
# Run it against a packaging staging tree by passing that tree's root:
#
#     scripts/check-distributable-clean.sh /path/to/staged/package
#
# With no argument it checks this working tree, which is expected to FAIL for a developer
# who has run setup-dev-assets — that is the check proving it can tell the difference.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${1:-$(CDPATH= cd -- "$script_dir/.." && pwd)}

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

if [ "$status" -eq 0 ]; then
    echo "Asset directories are empty of content. Safe to package."
else
    echo "" >&2
    echo "Refusing to call this package clean. Empty the asset directories before packaging." >&2
fi

exit "$status"
