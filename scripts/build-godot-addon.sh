#!/bin/sh
# Build the Godot addon (godot/addons/godogbg) for one or more platforms,
# in both the template_debug and template_release variants.
#
#   scripts/build-godot-addon.sh linux-x86_64 windows-x86_64 android-arm64
#
# windows-x86_64 and android-arm64 need the static glib from
# scripts/build-glib.sh first; android-arm64 also needs $ANDROID_NDK_HOME.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)

[ $# -gt 0 ] || set -- linux-x86_64

for platform in "$@"; do
    case $platform in
    linux-x86_64)   toolchain= ;;
    windows-x86_64) toolchain=$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake ;;
    android-arm64)  toolchain=$ROOT/cmake/toolchains/android-arm64.cmake ;;
    *) echo "unknown platform: $platform" >&2; exit 1 ;;
    esac

    for variant in debug release; do
        build=$ROOT/build/godot-$platform-$variant
        cmake -S "$ROOT" -B "$build" ${toolchain:+--toolchain "$toolchain"} \
            -DGNUBG_BUILD_GODOT=ON \
            -DGNUBG_BUILD_CLI=OFF \
            -DGODOTCPP_TARGET=template_$variant
        cmake --build "$build" -j"$(nproc)"
    done
done

ls -l "$ROOT/godot/addons/godogbg/bin"
