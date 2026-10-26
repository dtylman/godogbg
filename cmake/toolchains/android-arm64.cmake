# CMake toolchain for Android arm64-v8a: the NDK's own toolchain file, plus
# the static glib built by scripts/build-glib.sh.
#
#   ANDROID_NDK_HOME=~/Android/Sdk/ndk/28.2.13676358 scripts/build-glib.sh android-arm64
#   cmake -S . -B build/android-arm64 \
#       --toolchain cmake/toolchains/android-arm64.cmake \
#       -DANDROID_NDK=$HOME/Android/Sdk/ndk/28.2.13676358
#
# ANDROID_NDK defaults to $ANDROID_NDK_HOME. ANDROID_PLATFORM must not be
# lower than the API level glib was built for (default 21).

if(NOT ANDROID_NDK)
    if(DEFINED ENV{ANDROID_NDK_HOME})
        set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
    elseif(DEFINED ENV{ANDROID_NDK_ROOT})
        set(ANDROID_NDK "$ENV{ANDROID_NDK_ROOT}")
    else()
        message(FATAL_ERROR "Set ANDROID_NDK or ANDROID_NDK_HOME to the NDK directory")
    endif()
endif()
set(ANDROID_NDK "${ANDROID_NDK}" CACHE PATH "Android NDK directory")

set(ANDROID_ABI arm64-v8a CACHE STRING "Android ABI")
set(ANDROID_PLATFORM android-21 CACHE STRING "Minimum Android API level")

include("${ANDROID_NDK}/build/cmake/android.toolchain.cmake")

get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(GNUBG_DEPS_PREFIX "${_repo}/build/deps/android-arm64"
    CACHE PATH "Prefix holding the Android static glib")

# The NDK restricts library lookups to its sysroot.
list(APPEND CMAKE_FIND_ROOT_PATH "${GNUBG_DEPS_PREFIX}")

# pkg-config must only see the cross-built .pc files.
set(ENV{PKG_CONFIG_LIBDIR} "${GNUBG_DEPS_PREFIX}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
