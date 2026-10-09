#!/usr/bin/env bash
# build.sh - configure + build Spawn (Standalone) on macOS using CMake presets.
# Usage:  ./build.sh [debug|release]   (defaults to debug)
#
# On macOS the toolchain is Apple Clang from Xcode Command Line Tools and ninja.
# This script locates a working cmake (preferring arm64) and puts a matching
# arm64 ninja on PATH, then invokes the matching preset.
set -euo pipefail

CFG="${1:-debug}"

case "$CFG" in
    debug|release) ;;
    *) echo "Usage: $0 [debug|release]" >&2; exit 1 ;;
esac

# Locate a working cmake. Order:
#   1. $CMAKE (env override)
#   2. arm64 Homebrew cmake (Apple Silicon native)
#   3. cmake on PATH (last resort; may be the wrong arch)
if [[ -n "${CMAKE:-}" ]]; then
    CMAKE_BIN="$CMAKE"
elif [[ -x /opt/homebrew/bin/cmake ]]; then
    CMAKE_BIN=/opt/homebrew/bin/cmake
else
    CMAKE_BIN=$(command -v cmake)
fi

if [[ -z "$CMAKE_BIN" ]]; then
    echo "cmake not found. Install Xcode Command Line Tools or Homebrew cmake." >&2
    exit 1
fi

# Detect arch mismatch — common when /usr/local/bin/cmake is x86 on Apple Silicon.
CMAKE_ARCH=$(file "$CMAKE_BIN" | grep -oE '(arm64|x86_64)' | head -1 || true)
HOST_ARCH=$(uname -m)
if [[ -n "$CMAKE_ARCH" && "$CMAKE_ARCH" != "$HOST_ARCH" ]]; then
    echo "Warning: cmake at $CMAKE_BIN is $CMAKE_ARCH but host is $HOST_ARCH." >&2
    echo "This usually fails with 'bad CPU type in executable'. Install an arm64 cmake:" >&2
    echo "  brew install cmake" >&2
fi

# Find a ninja whose arch matches the host. Prepend its directory to PATH so
# the Ninja generator picks it up.
NINJA_DIR=""
if [[ "$HOST_ARCH" == "arm64" ]]; then
    if [[ -x /Applications/CLion.app/Contents/bin/ninja/mac/aarch64/ninja ]]; then
        NINJA_DIR=/Applications/CLion.app/Contents/bin/ninja/mac/aarch64
    elif [[ -x /opt/homebrew/bin/ninja ]]; then
        NINJA_DIR=/opt/homebrew/bin
    fi
fi
if [[ -z "$NINJA_DIR" ]]; then
    if command -v ninja >/dev/null 2>&1; then
        NINJA_DIR="$(dirname "$(command -v ninja)")"
    fi
fi

if [[ -z "$NINJA_DIR" ]]; then
    echo "ninja not found. Install with: brew install ninja" >&2
    exit 1
fi

NINJA_BIN="$NINJA_DIR/ninja"
NINJA_ARCH=$(file "$NINJA_BIN" | grep -oE '(arm64|x86_64)' | head -1 || true)
if [[ -n "$NINJA_ARCH" && "$NINJA_ARCH" != "$HOST_ARCH" ]]; then
    echo "Warning: ninja at $NINJA_BIN is $NINJA_ARCH but host is $HOST_ARCH." >&2
    echo "Install an arm64 ninja: brew install ninja" >&2
fi

export PATH="$NINJA_DIR:$PATH"

# Pick a known-good macOS SDK. The CLT 27.0 SDK ships a malformed
# libSystem.B.tbd that breaks the linker. Prefer the 26.5 SDK if available.
# Override with SPAWN_OSX_SYSROOT=... to point at a different SDK.
# We export SDKROOT so nested CMake projects (e.g. JUCE's juceaide sub-build)
# also see it, since they don't always inherit CMAKE_OSX_SYSROOT.
if [[ -z "${SPAWN_OSX_SYSROOT:-}" ]]; then
    SDK_265="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk"
    if [[ -d "$SDK_265" ]]; then
        SPAWN_OSX_SYSROOT="$SDK_265"
    fi
fi

CMAKE_EXTRA_ARGS=()
if [[ -n "${SPAWN_OSX_SYSROOT:-}" ]]; then
    CMAKE_EXTRA_ARGS+=("-DCMAKE_OSX_SYSROOT=$SPAWN_OSX_SYSROOT")
    export SDKROOT="$SPAWN_OSX_SYSROOT"
    echo "Using macOS SDK: $SPAWN_OSX_SYSROOT"
fi

echo "=== Configure ($CFG) using $CMAKE_BIN (ninja from $NINJA_DIR) ==="
"$CMAKE_BIN" --preset="$CFG" "${CMAKE_EXTRA_ARGS[@]}"

echo "=== Build ==="
"$CMAKE_BIN" --build --preset="$CFG"