# Godot GDExtension wrapper: a "Gnubg" engine singleton for GDScript.
#
# Built from the top-level CMakeLists.txt with -DGNUBG_BUILD_GODOT=ON.
# GODOTCPP_TARGET (template_debug or template_release) picks the variant;
# the library is written to godot/addons/godogbg/bin/ under the file name
# that godogbg.gdextension expects.

enable_language(CXX)

include(FetchContent)

# godot-cpp 4.5 works with Godot 4.5 and newer (GDExtension is forward
# compatible), so the addon sets compatibility_minimum = "4.5".
FetchContent_Declare(godot-cpp
    URL https://github.com/godotengine/godot-cpp/archive/e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77.tar.gz
    URL_HASH SHA256=d4c03daf46c8bef4544614182b0762c3e32789151c7f8f44e8eb2053d5cddeba
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)  # godot-4.5-stable

# Generate bindings only for the classes the wrapper uses: much faster builds.
set(GODOTCPP_BUILD_PROFILE ${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json
    CACHE PATH "godot-cpp build profile")
FetchContent_MakeAvailable(godot-cpp)

set(GODOGBG_ADDON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/addons/godogbg)

add_library(godogbg SHARED
    src/gnubg_godot.cpp
    src/register_types.cpp)
target_link_libraries(godogbg PRIVATE godot-cpp gnubg_static)

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set_target_properties(godogbg PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    PREFIX "lib"
    OUTPUT_NAME "godogbg${GODOTCPP_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${GODOGBG_ADDON_DIR}/bin>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${GODOGBG_ADDON_DIR}/bin>")

if(NOT APPLE)
    # Export only the GDExtension entry point, nothing from glib or libstdc++.
    target_link_options(godogbg PRIVATE
        -Wl,--exclude-libs,ALL
        -Wl,--no-undefined)
endif()
if(MINGW)
    # No import library next to the DLL in the addon directory.
    set_target_properties(godogbg PROPERTIES IMPORT_SUFFIX ".dll.a"
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

# The engine's runtime files go into the addon too (gitignored there).
add_custom_command(TARGET godogbg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/data ${GODOGBG_ADDON_DIR}/data
    VERBATIM)
