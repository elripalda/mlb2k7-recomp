# MLB 2K7 Recomp - ReXGlue Recompiled Project
# The CMake target keeps the codegen name (MLB2K7); the exe is MLB2K7Recomp.exe.

cmake_minimum_required(VERSION 3.25)
project(MLB2K7 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

set(MLB2K7_SOURCES
    src/main.cpp
    src/mlb_app.cpp
    src/fps_counter.cpp
    src/gpu_fix.cpp
    src/display.cpp
    src/xmp_music.cpp
    src/music_win.cpp
    src/platform_win.cpp
    src/setup.cpp
    src/xiso.cpp
)

if(WIN32)
    add_executable(MLB2K7 WIN32 ${MLB2K7_SOURCES} res/app.rc)
else()
    add_executable(MLB2K7 ${MLB2K7_SOURCES})
endif()

set_target_properties(MLB2K7 PROPERTIES OUTPUT_NAME MLB2K7Recomp)
if(WIN32)
    target_link_libraries(MLB2K7 PRIVATE dbghelp mfplat mfuuid ole32 oleaut32 shell32 uuid)
    # Keep a .pdb so crash addresses can be turned into game function names.
    target_link_options(MLB2K7 PRIVATE "LINKER:/DEBUG")
endif()

rexglue_setup_target(MLB2K7)
