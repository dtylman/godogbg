cmake_minimum_required(VERSION 3.16)

project(gnubg_engine VERSION 1.08.003 LANGUAGES C)

set(GNUBG_SIMD "auto" CACHE STRING "SIMD backend: auto, none, sse2, avx, neon")
set_property(CACHE GNUBG_SIMD PROPERTY STRINGS auto none sse2 avx neon)
option(GNUBG_STATIC_GLIB "Link glib statically into the shared library" ON)
option(GNUBG_BUILD_CLI "Build the gnubg-cli test program" ON)

# gnubg sources #include "config.h", which the compiler looks up next to the
# source file first: a leftover from ./configure would shadow port/config.h.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gnubg/config.h)
    message(FATAL_ERROR "gnubg/config.h exists (from ./configure); remove it, "
        "or run 'make distclean' in gnubg/")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# glib (the only external dependency)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.22)

if(GNUBG_STATIC_GLIB)
    # Resolve every library from `pkg-config --static` to its .a archive,
    # leaving only system libraries (libc, libm, pthread, Win32 DLLs) dynamic.
    set(GLIB_LINK_LIBRARIES "")
    set(_system_libs c m dl rt pthread log)
    foreach(lib IN LISTS GLIB_STATIC_LIBRARIES)
        if(lib IN_LIST _system_libs)
            list(APPEND GLIB_LINK_LIBRARIES ${lib})
            continue()
        endif()
        find_library(GNUBG_STATIC_${lib}
            NAMES lib${lib}.a
            HINTS ${GLIB_STATIC_LIBRARY_DIRS}
            NO_CMAKE_SYSTEM_PATH)
        if(NOT GNUBG_STATIC_${lib})
            find_library(GNUBG_STATIC_${lib} NAMES lib${lib}.a)
        endif()
        if(GNUBG_STATIC_${lib})
            list(APPEND GLIB_LINK_LIBRARIES ${GNUBG_STATIC_${lib}})
        else()
            list(APPEND GLIB_LINK_LIBRARIES ${lib})
        endif()
    endforeach()
    list(APPEND GLIB_LINK_LIBRARIES ${GLIB_STATIC_LDFLAGS_OTHER})
    set(GLIB_COMPILE_DEFINITIONS GLIB_STATIC_COMPILATION)
    set(GLIB_COMPILE_OPTIONS ${GLIB_STATIC_CFLAGS_OTHER})
else()
    set(GLIB_LINK_LIBRARIES ${GLIB_LINK_LIBRARIES})
    set(GLIB_COMPILE_DEFINITIONS "")
    set(GLIB_COMPILE_OPTIONS ${GLIB_CFLAGS_OTHER})
endif()

# ---------------------------------------------------------------------------
# SIMD

if(GNUBG_SIMD STREQUAL "auto")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cpu)
    if(_cpu MATCHES "^(x86_64|amd64)$")
        set(GNUBG_SIMD sse2)
    elseif(_cpu MATCHES "^(aarch64|arm64)$")
        set(GNUBG_SIMD neon)
    else()
        set(GNUBG_SIMD none)
    endif()
endif()
message(STATUS "gnubg SIMD backend: ${GNUBG_SIMD}")

set(SIMD_DEFINITIONS "")
set(SIMD_OPTIONS "")
if(GNUBG_SIMD STREQUAL "sse2")
    set(SIMD_DEFINITIONS GNUBG_SIMD_SSE2)
    set(SIMD_OPTIONS -msse -msse2)
elseif(GNUBG_SIMD STREQUAL "avx")
    set(SIMD_DEFINITIONS GNUBG_SIMD_AVX)
    set(SIMD_OPTIONS -mavx)
elseif(GNUBG_SIMD STREQUAL "neon")
    set(SIMD_DEFINITIONS GNUBG_SIMD_NEON)
elseif(NOT GNUBG_SIMD STREQUAL "none")
    message(FATAL_ERROR "Unknown GNUBG_SIMD value: ${GNUBG_SIMD}")
endif()

# ---------------------------------------------------------------------------
# Engine sources: unmodified GNU Backgammon files

set(GNUBG_SOURCES
    gnubg/bearoff.c
    gnubg/bearoffgammon.c
    gnubg/eval.c
    gnubg/matchequity.c
    gnubg/matchid.c
    gnubg/mec.c
    gnubg/mtsupport.c
    gnubg/osr.c
    gnubg/positionid.c
    gnubg/lib/cache.c
    gnubg/lib/inputs.c
    gnubg/lib/isaac.c
    gnubg/lib/list.c
    gnubg/lib/md5.c
    gnubg/lib/neuralnet.c
    gnubg/lib/neuralnetsse.c
    gnubg/lib/SFMT.c
)

set(PORT_SOURCES
    port/hooks.c
    src/gnubg_api.c
)

add_library(gnubg_objects OBJECT ${GNUBG_SOURCES} ${PORT_SOURCES})
set_target_properties(gnubg_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
target_include_directories(gnubg_objects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/port
        ${CMAKE_CURRENT_SOURCE_DIR}/gnubg
        ${CMAKE_CURRENT_SOURCE_DIR}/gnubg/lib
        ${GLIB_INCLUDE_DIRS})
target_compile_definitions(gnubg_objects PRIVATE
    G_DISABLE_ASSERT
    ${GLIB_COMPILE_DEFINITIONS}
    ${SIMD_DEFINITIONS}
    GNUBG_API_BUILD_SHARED)
target_compile_options(gnubg_objects PRIVATE
    ${GLIB_COMPILE_OPTIONS}
    ${SIMD_OPTIONS}
    $<$<CONFIG:Release>:-O3>
    -ffast-math
    -fno-strict-aliasing
    $<$<BOOL:${MINGW}>:-mms-bitfields>)

# Static library: link it together with glib (see GLIB_LINK_LIBRARIES).
add_library(gnubg_static STATIC $<TARGET_OBJECTS:gnubg_objects>)
set_target_properties(gnubg_static PROPERTIES OUTPUT_NAME gnubg)
target_include_directories(gnubg_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gnubg_static PUBLIC ${GLIB_LINK_LIBRARIES} $<$<NOT:$<BOOL:${WIN32}>>:m>)

# Shared library: self-contained, exports only the gnubg_* API.
add_library(gnubg_shared SHARED $<TARGET_OBJECTS:gnubg_objects>)
set_target_properties(gnubg_shared PROPERTIES OUTPUT_NAME gnubg)
if(WIN32)
    # The import library would clash with the static libgnubg.a.
    set_target_properties(gnubg_shared PROPERTIES PREFIX "" IMPORT_SUFFIX ".dll.a")
endif()
target_include_directories(gnubg_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gnubg_shared PRIVATE ${GLIB_LINK_LIBRARIES} $<$<NOT:$<BOOL:${WIN32}>>:m>)
if(NOT APPLE)
    target_link_options(gnubg_shared PRIVATE
        -Wl,--exclude-libs,ALL
        -Wl,--no-undefined
        $<$<OR:$<BOOL:${MINGW}>,$<BOOL:${ANDROID}>>:-static-libgcc>)
endif()

if(GNUBG_BUILD_CLI)
    add_executable(gnubg-cli tools/gnubg_cli.c)
    target_link_libraries(gnubg-cli PRIVATE gnubg_shared)
    if(NOT WIN32)
        set_target_properties(gnubg-cli PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endif()
