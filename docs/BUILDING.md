# Building

All builds use CMake from a Debian 12 host.

## Outputs

| File | What it is |
|---|---|
| `libgnubg.so` / `gnubg.dll` | Self-contained shared library that exports only `gnubg_*`. Load this from a GDExtension or via FFI. |
| `libgnubg.dll.a` | Windows only: import library for `gnubg.dll`. |
| `libgnubg.a` | Static archive of the engine. Link it together with glib, for example into your GDExtension library, to ship a single binary. |
| `gnubg-cli` | Test driver and usage example ([tools/gnubg_cli.c](../tools/gnubg_cli.c)). |
| `test-legal` | Checks the legal-move API against the engine's `GenerateMoves()` ([tools/test_legal.c](../tools/test_legal.c)). |

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `GNUBG_SIMD` | `auto` | `auto`, `none`, `sse2`, `avx` or `neon`. `auto` picks `sse2` on x86_64, `neon` on arm64 and `none` otherwise. |
| `GNUBG_STATIC_GLIB` | `ON` | Link glib and its dependencies (pcre2, plus libintl/libiconv on Windows and Android) statically into the shared library. |
| `GNUBG_BUILD_CLI` | `ON` | Build `gnubg-cli` and `test-legal`. |
| `GNUBG_BUILD_GODOT` | `OFF` | Also build the Godot GDExtension in [godot/](../godot/) (needs a C++ compiler; downloads godot-cpp). See [GODOT.md](GODOT.md). |
| `GODOTCPP_TARGET` | `template_debug` | With `GNUBG_BUILD_GODOT`: `template_debug` or `template_release`. |

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
build/linux-x86_64/gnubg-cli data legal 4HPwATDgc/ABMA 3 1   # legal moves and steps
build/linux-x86_64/test-legal data 20       # ends with "0 failure(s)"
```

Set `GNUBG_SKILL=0..7` to run `gnubg-cli` at another level (0 = beginner,
5 = world class, 7 = grandmaster).

## Windows x86_64 (mingw-w64)

Cross-compiled from Linux with Debian's mingw-w64. Debian doesn't package
glib for mingw, so a script cross-builds a static glib first.

```bash
sudo apt install gcc-mingw-w64-x86-64 meson ninja-build pkg-config git curl
sudo apt install g++-mingw-w64-x86-64   # only for the Godot addon
sudo apt install wine wine64            # optional, to run gnubg-cli.exe
```

C is compiled with the default (win32 thread model) compiler. C++, used only
by the Godot addon, uses `x86_64-w64-mingw32-g++-posix`, because GCC 12's
win32 variant has no `std::mutex`. The toolchain file selects both.

Build glib 2.74.6 (the same version as Debian 12's) as a static library. The
script downloads the glib tarball (checking its sha256), and meson fetches
pcre2, libffi, zlib and proxy-libintl through glib's bundled wraps, so it needs
network access. The result is installed in `build/deps/mingw-x86_64`. This
only needs to run once.

```bash
scripts/build-glib.sh mingw-x86_64
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

Built with the Android NDK (r28c tested; use r28 or newer, since those align
libraries to 16 KB pages, as Android 15+ requires). The minimum Android
version is **API 21** (Android 5.0), which works with any Godot 4 export
preset.

Android's libc only has `iconv` from API 28, so the script also cross-builds
GNU libiconv 1.17 (static, sha256-checked) before glib. It needs the same
packages as the Windows build, minus mingw: `meson ninja-build pkg-config git
curl make`.

```bash
export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/28.2.13676358
scripts/build-glib.sh android-arm64          # once; ANDROID_API=21 by default
cmake -S . -B build/android-arm64 \
    --toolchain cmake/toolchains/android-arm64.cmake
cmake --build build/android-arm64 -j
```

The toolchain file wraps the NDK's `android.toolchain.cmake`. It sets
`ANDROID_ABI=arm64-v8a` and `ANDROID_PLATFORM=android-21` (don't go lower
than the `ANDROID_API` glib was built for), and points pkg-config at
`build/deps/android-arm64`. The SIMD backend is NEON.

Outputs: `libgnubg.so`, `libgnubg.a` and `gnubg-cli`. The library needs only
`libc.so`, `libm.so` and `libdl.so`. Check the result:

```bash
R=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin
$R/llvm-readelf -d build/android-arm64/libgnubg.so | grep NEEDED
$R/llvm-readelf -l build/android-arm64/libgnubg.so | grep LOAD   # align 0x4000
$R/llvm-nm -D --defined-only build/android-arm64/libgnubg.so      # only gnubg_*
```

To run `gnubg-cli` on a device or emulator, push it next to `libgnubg.so`
(it has a `$ORIGIN` runpath) together with `data/`. An x86_64 emulator image
of API 30 or newer runs arm64 binaries through its translation layer
(`ro.enable.native.bridge.exec=1`), so no arm64 hardware is needed:

```bash
emulator -avd Pixel_8 -read-only -no-window -no-audio &
adb wait-for-device
adb shell mkdir -p /data/local/tmp/gnubg
adb push build/android-arm64/libgnubg.so build/android-arm64/gnubg-cli data \
    /data/local/tmp/gnubg/
adb shell 'cd /data/local/tmp/gnubg && ./gnubg-cli data best 4HPwATDgc/ABMA 3 1'
adb emu kill
```

The output matches Linux. As on Windows, self-play games can differ on exact
equity ties because bionic's `qsort` orders equal elements differently.

`libgnubg.so` is about 2.6 MB. Strip it with `$R/llvm-strip` to get it to
about 1.9 MB.

## Godot addon

`scripts/build-godot-addon.sh PLATFORM...` builds the GDExtension in
`godot/addons/godogbg` for each platform (`linux-x86_64`, `windows-x86_64`,
`android-arm64`), in both the `template_debug` and `template_release`
variants. Windows and Android need their static glib from
`scripts/build-glib.sh` first. This also needs a C++ compiler for the target:
`g++` on Linux, `g++-mingw-w64-x86-64` for Windows (the NDK already has one).

```bash
scripts/build-godot-addon.sh linux-x86_64 windows-x86_64
ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/28.2.13676358 \
    scripts/build-godot-addon.sh android-arm64
```

Each platform's addon library contains the engine, glib and godot-cpp, and
exports only the GDExtension entry point (plus `gnubg_*` on Windows). It
depends only on system libraries: libc, libm and libmvec on Linux; Windows
system DLLs; and libc, libm and libdl on Android. See [GODOT.md](GODOT.md) for
the GDScript API and testing.

## Updating the engine sources

`gnubg/` holds the unmodified gnubg release tarball. Don't run `./configure`
in it: the generated `gnubg/config.h` would shadow `port/config.h`, and CMake
refuses to build while it exists. To update, replace `gnubg/` with a newer
release, fix the source list in `CMakeLists.txt` if files moved, then rebuild
and compare `gnubg-cli` output against the gnubg binary from that release.
