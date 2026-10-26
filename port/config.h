/*
 * Hand-written replacement for the autotools-generated config.h of
 * GNU Backgammon, used to build the evaluation engine as a library.
 *
 * SIMD selection is done by CMake through one of the compile definitions
 * GNUBG_SIMD_SSE2, GNUBG_SIMD_AVX or GNUBG_SIMD_NEON (none = plain C).
 */

#ifndef GNUBG_PORT_CONFIG_H
#define GNUBG_PORT_CONFIG_H

#define PACKAGE "gnubg"
#define VERSION "1.08.003"
#define GETTEXT_PACKAGE "gnubg"

/* Compiler features (GCC and Clang, including mingw and the Android NDK) */
#define HAVE_FUNC_ATTRIBUTE_CONST 1
#define HAVE_FUNC_ATTRIBUTE_FORMAT 1
#define HAVE_FUNC_ATTRIBUTE_PURE 1
#define HAVE_FUNC_ATTRIBUTE_UNUSED 1
#define HAVE_FUNC_ATTRIBUTE_FALLTHROUGH 1
#define HAVE___ATTRIBUTE__ 1
#define HAVE___BUILTIN_CLZ 1
#define HAVE___BUILTIN_EXPECT 1

#define HAVE_LIBM 1
#define STDC_HEADERS 1

#if !defined(_WIN32)
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_SYS_TIME_H 1
#define HAVE_UNISTD_H 1
#else
#define HAVE__ALIGNED_MALLOC 1
#endif

/* No USE_MULTITHREAD: the library is driven from a single caller thread. */

/*
 * SSE2 is part of the x86_64 ABI and NEON of the arm64 ABI, so the runtime
 * CPU test (which may call exit() or install a SIGILL handler) is disabled
 * for those. AVX keeps the test since not every x86_64 CPU has it.
 */
#if defined(GNUBG_SIMD_AVX)
#define USE_SIMD_INSTRUCTIONS 1
#define USE_AVX 1
#define HAVE_SSE 1
#elif defined(GNUBG_SIMD_SSE2)
#define USE_SIMD_INSTRUCTIONS 1
#define USE_SSE2 1
#define HAVE_SSE 1
#define DISABLE_SIMD_TEST 1
#elif defined(GNUBG_SIMD_NEON)
#define USE_SIMD_INSTRUCTIONS 1
#define USE_NEON 1
#define HAVE_NEON 1
#define DISABLE_SIMD_TEST 1
#endif

#endif /* GNUBG_PORT_CONFIG_H */
