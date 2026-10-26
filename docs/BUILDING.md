# Building

All builds use CMake from a Debian 12 host.

## Outputs

| File | What it is |
|---|---|
| `libgnubg.so` / `gnubg.dll` | Self-contained shared library that exports only `gnubg_*`. Load this from a GDExtension or via FFI. |
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

Not done yet. This needs a static glib built for mingw.

## Android arm64-v8a (NDK)

Not done yet. This needs a static glib built with the NDK, or a small glib shim.

## Updating the engine sources

`gnubg/` holds the unmodified gnubg release tarball. Don't run `./configure`
in it: the generated `gnubg/config.h` would shadow `port/config.h`, and CMake
refuses to build while it exists. To update, replace `gnubg/` with a newer
release, fix the source list in `CMakeLists.txt` if files moved, then rebuild
and compare `gnubg-cli` output against the gnubg binary from that release.
