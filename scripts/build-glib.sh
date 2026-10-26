#!/bin/sh
# Cross-build a static glib (with pcre2, libffi, zlib and proxy-libintl from
# meson wraps) for Windows x86_64 using the Debian mingw-w64 toolchain.
#
# Installs into build/deps/mingw-x86_64 (or $1). The CMake toolchain file
# cmake/toolchains/mingw-w64-x86_64.cmake points pkg-config there.
#
# Needs: gcc-mingw-w64-x86-64 meson ninja-build pkg-config git curl

set -eu

GLIB_VERSION=2.74.6
GLIB_SHA256=069cf7e51cd261eb163aaf06c8d1754c6835f31252180aff5814e5afc7757fbc

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${1:-$ROOT/build/deps/mingw-x86_64}
WORK=$ROOT/build/deps/src
CROSS=$ROOT/scripts/cross/mingw-w64-x86_64.ini

mkdir -p "$WORK"
cd "$WORK"

tarball=glib-$GLIB_VERSION.tar.xz
if [ ! -f "$tarball" ]; then
    curl -fSL -o "$tarball.part" \
        "https://download.gnome.org/sources/glib/${GLIB_VERSION%.*}/$tarball"
    mv "$tarball.part" "$tarball"
fi
echo "$GLIB_SHA256  $tarball" | sha256sum -c -
[ -d "glib-$GLIB_VERSION" ] || tar xf "$tarball"

# Keep the host's .pc files out of the cross build.
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
unset PKG_CONFIG_PATH

builddir=$WORK/glib-$GLIB_VERSION-mingw-x86_64
if [ ! -f "$builddir/build.ninja" ]; then
    meson setup "$builddir" "glib-$GLIB_VERSION" \
        --cross-file "$CROSS" \
        --prefix "$PREFIX" \
        --libdir lib \
        --buildtype release \
        --default-library static \
        --wrap-mode forcefallback \
        -Dtests=false \
        -Dinstalled_tests=false \
        -Dnls=disabled \
        -Dman=false \
        -Dgtk_doc=false \
        -Dsysprof=disabled \
        -Dlibmount=disabled \
        -Dselinux=disabled \
        -Dxattr=false
fi
meson compile -C "$builddir"
meson install -C "$builddir" --quiet

echo "glib $GLIB_VERSION installed in $PREFIX"
