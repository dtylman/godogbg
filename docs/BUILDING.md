# Building

All builds use CMake from a Debian 12 host.

## Outputs

| File | What it is |
|---|---|
| `libgnubg.so` / `gnubg.dll` | Self-contained shared library that exports only `gnubg_*`. Load this from a GDExtension or via FFI. |
| `libgnubg.dll.a` | Windows only: import library for `gnubg.dll`. |
| `libgnubg.a` | Static archive of the engine. Link it together with glib, for example into your GDExtension library, to ship a single binary. |
| `gnubg-cli` | Test driver and usage example ([tools/gnubg_cli.c](../tools/gnubg_cli.c)). |

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `GNUBG_SIMD` | `auto` | `auto`, `none`, `sse2`, `avx` or `neon`. `auto` picks `sse2` on x86_64, `neon` on arm64 and `none` otherwise. |
| `GNUBG_STATIC_GLIB` | `ON` | Link glib (and pcre2) statically into the shared library. |
| `GNUBG_BUILD_CLI` | `ON` | Build `gnubg-cli`. |

Use `avx` only if every target CPU supports AVX. The engine checks at start-up
and **exits the process** if AVX is missing. `sse2` and `neon` are always
available on their architectures, so they skip that check.

## Linux x86_64

```bash
sudo apt install build-essential cmake pkg-config libglib2.0-dev
cmake -S . -B build/linux-x86_64
cmake --build build/linux-x86_64 -j
```

Check the result:

```bash
ldd build/linux-x86_64/libgnubg.so            # only libc, libm, libmvec
nm -D --defined-only build/linux-x86_64/libgnubg.so   # only gnubg_* symbols
build/linux-x86_64/gnubg-cli data best 4HPwATDgc/ABMA 3 1
build/linux-x86_64/gnubg-cli data selfplay 42
```

Set `GNUBG_SKILL=0..7` to run `gnubg-cli` at another level (0 = beginner,
5 = world class, 7 = grandmaster).

## Windows x86_64 (mingw-w64)

Cross-compiled from Linux with Debian's mingw-w64 (win32 thread model). Debian
doesn't package glib for mingw, so a script cross-builds a static glib first.

```bash
sudo apt install gcc-mingw-w64-x86-64 meson ninja-build pkg-config git curl
sudo apt install wine wine64      # optional, to run gnubg-cli.exe
```

Build glib 2.74.6 (the same version as Debian 12's) as a static library. The
script downloads the glib tarball (checking its sha256), and meson fetches
pcre2, libffi, zlib and proxy-libintl through glib's bundled wraps, so it needs
network access. The result is installed in `build/deps/mingw-x86_64`. This
only needs to run once.

```bash
scripts/build-glib-mingw.sh
```

Then build with the toolchain file, which points pkg-config only at that
prefix (override it with `-DGNUBG_DEPS_PREFIX=...`):

```bash
cmake -S . -B build/windows-x86_64 \
    --toolchain cmake/toolchains/mingw-w64-x86_64.cmake
cmake --build build/windows-x86_64 -j
```

Outputs: `gnubg.dll`, its import library `libgnubg.dll.a`, the static
`libgnubg.a` and `gnubg-cli.exe`. The DLL only imports Windows system DLLs
(`KERNEL32`, `msvcrt`, `ADVAPI32`, `SHELL32`, `USER32`, `ole32`, `WS2_32`), so
there is no libgcc or libwinpthread to ship.

Check the result:

```bash
x86_64-w64-mingw32-objdump -p build/windows-x86_64/gnubg.dll | grep 'DLL Name'
x86_64-w64-mingw32-objdump -p build/windows-x86_64/gnubg.dll \
    | sed -n '/Ordinal\/Name Pointer/,/^$/p'     # only gnubg_* symbols
export WINEDEBUG=-all WINEPREFIX=$PWD/build/wineprefix
wine build/windows-x86_64/gnubg-cli.exe data best 4HPwATDgc/ABMA 3 1
```

Evaluations, best moves and cube decisions are the same as on Linux. Self-play
games with the same seed can still differ between platforms: when two moves
have exactly the same equity (in practice, positions that are already won or
lost), gnubg's `qsort` breaks the tie, and msvcrt's `qsort` orders equal
elements differently from glibc's.

`gnubg.dll` is about 2 MB. Strip it with `x86_64-w64-mingw32-strip` to get it
to about 1.2 MB.

## Android arm64-v8a (NDK)

Not done yet. This needs a static glib built with the NDK, or a small glib shim.

## Updating the engine sources

`gnubg/` holds the unmodified gnubg release tarball. Don't run `./configure`
in it: the generated `gnubg/config.h` would shadow `port/config.h`, and CMake
refuses to build while it exists. To update, replace `gnubg/` with a newer
release, fix the source list in `CMakeLists.txt` if files moved, then rebuild
and compare `gnubg-cli` output against the gnubg binary from that release.
