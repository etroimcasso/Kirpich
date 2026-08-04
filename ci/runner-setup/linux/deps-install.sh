#!/bin/sh
# Linux self-hosted runner — build-dependency setup. Run once per runner, as root.
#
# One script for both architectures: x64 and ARM64 need the same packages, so it branches on
# `uname -m` only where something genuinely differs. Nothing here touches the runner service or its
# labels; it installs the toolchain the CI jobs expect to find.
#
# Installs:
#   - git, cmake (3.28+), ninja, ccache   — build system and compiler cache
#   - g++ 13 or newer                     — the C++20 floor the top-level CMakeLists enforces
#   - SDL3's X11/Wayland build inputs     — SDL is built from source inside the engine submodule
#   - python3                             — port-time tooling
#
# SDL3, SameBoy, spdlog and GoogleTest are all built from source (submodule or FetchContent), so
# there is nothing to install for them beyond their own build inputs.

set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "error: run as root (sudo $0)" >&2
    exit 1
fi

arch=$(uname -m)
echo "== Kirpich runner setup — Linux ${arch} =="

if ! command -v apt-get >/dev/null 2>&1; then
    echo "error: this script assumes apt. Install the equivalents by hand on other distributions:" >&2
    echo "       git cmake ninja ccache g++-13 python3 + SDL3's X11/Wayland dev packages" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -y

apt-get install -y \
    git \
    cmake \
    ninja-build \
    ccache \
    build-essential \
    g++-13 \
    python3 \
    pkg-config

# SDL3 build inputs. libxtst-dev is the one that is easy to miss — SDL3's XTEST support needs it and
# its absence surfaces late, as a configure failure inside the engine's SDL build rather than
# anything naming SDL.
apt-get install -y \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols \
    libasound2-dev libpulse-dev \
    libgl1-mesa-dev libegl1-mesa-dev

# Headless test steps set SDL_VIDEODRIVER=offscreen, which still wants a working GL/EGL stack.
apt-get install -y libgl1-mesa-dri

echo ""
echo "Done. Verify with:"
echo "    cmake --version     # 3.28 or newer"
echo "    g++-13 --version    # 13 or newer"
echo "    ninja --version"
echo ""
echo "Then place the ROM (see docs/features/ci.md):"
echo "    mkdir -p ~/ci-assets/kirpich && cp <your-rom>.gb ~/ci-assets/kirpich/tetris.gb"
