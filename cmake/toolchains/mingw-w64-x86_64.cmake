# CMake toolchain for Windows x86_64 with the Debian mingw-w64 cross compiler.
#
#   scripts/build-glib.sh mingw-x86_64
#   cmake -S . -B build/windows-x86_64 \
#       --toolchain cmake/toolchains/mingw-w64-x86_64.cmake
#
# GNUBG_DEPS_PREFIX is where scripts/build-glib.sh installed glib.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${_triple}-gcc)
# C++ (the Godot addon) needs std::mutex, which GCC 12 only has with the posix
# thread model; Debian's default g++ is the win32 one. C stays win32, so
# gnubg.dll doesn't need libwinpthread.
set(CMAKE_CXX_COMPILER ${_triple}-g++-posix)
set(CMAKE_RC_COMPILER ${_triple}-windres)

get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(GNUBG_DEPS_PREFIX "${_repo}/build/deps/mingw-x86_64"
    CACHE PATH "Prefix holding the mingw static glib")

set(CMAKE_FIND_ROOT_PATH /usr/${_triple} ${GNUBG_DEPS_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config must only see the cross-built .pc files.
set(ENV{PKG_CONFIG_LIBDIR} "${GNUBG_DEPS_PREFIX}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
