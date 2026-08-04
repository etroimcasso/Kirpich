#!/bin/sh
# macOS self-hosted runner — build-dependency setup. Run once per runner.
#
# Apple Silicon is what this fleet uses, but the script detects the architecture rather than
# assuming it, so an Intel machine works too. Nothing here touches the runner service or its labels.
#
# Installs via Homebrew:
#   - cmake (3.28+), ninja, ccache
#
# The compiler is Apple Clang from the Command Line Tools, not Homebrew — the jobs build with the
# system toolchain. SDL3, SameBoy, spdlog and GoogleTest are built from source (submodule or
# FetchContent), so there is nothing to install for them.

set -eu

arch=$(uname -m)
echo "== Kirpich runner setup — macOS ${arch} =="

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Command Line Tools are not installed. Installing — accept the dialog, then re-run this." >&2
    xcode-select --install
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "error: Homebrew not found. Install it from https://brew.sh, then re-run." >&2
    exit 1
fi

brew install cmake ninja ccache

echo ""
echo "Done. Verify with:"
echo "    cmake --version        # 3.28 or newer"
echo "    clang++ --version      # AppleClang 15 or newer"
echo "    ninja --version"
echo ""
echo "Then place the ROM (see docs/features/ci.md):"
echo "    mkdir -p ~/ci-assets/kirpich && cp <your-rom>.gb ~/ci-assets/kirpich/tetris.gb"
