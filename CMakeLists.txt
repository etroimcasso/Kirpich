cmake_minimum_required(VERSION 3.28)

# ── Apple target ──────────────────────────────────────────────────────────────
# Both are set before project() because that is when the compiler is probed, and a deployment
# target or architecture list arriving afterwards is only half-applied.
#
# arm64 only. Apple silicon is the whole of the current Mac line, there is no Intel machine here to
# test a x86_64 slice on, and shipping a slice nobody has run is worse than not shipping it.
# Big Sur is where arm64 starts, so it is the floor.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")
endif()

project(kirpich
    VERSION 0.9.2
    DESCRIPTION "C++ port of kaspermeerts/tetris (Game Boy DMG)"
    LANGUAGES CXX
)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Default build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ── Executables land at the build root ────────────────────────────────────────
# Targets are defined in subdirectories (src/), and without this CMake mirrors that
# layout into the build tree — the game would land at build/src/kirpich. A normal build
# puts its executables at the top: build/kirpich.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ── Platform detection ────────────────────────────────────────────────────────
if(APPLE)
    set(KIRPICH_PLATFORM "macos")
elseif(WIN32)
    set(KIRPICH_PLATFORM "windows")
elseif(UNIX)
    set(KIRPICH_PLATFORM "linux")
else()
    message(FATAL_ERROR "kirpich: unsupported platform")
endif()

message(STATUS "kirpich: platform   = ${KIRPICH_PLATFORM}")
message(STATUS "kirpich: compiler   = ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "kirpich: C++ std    = ${CMAKE_CXX_STANDARD}")
message(STATUS "kirpich: build type = ${CMAKE_BUILD_TYPE}")

# ── Compiler version floors ───────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
    message(FATAL_ERROR "GCC 13+ required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
    message(FATAL_ERROR "Clang 16+ required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
    message(FATAL_ERROR "AppleClang 15+ required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.38")
    message(FATAL_ERROR "MSVC 19.38+ (Visual Studio 2022 17.8+) required")
endif()

# ── Compile flags ─────────────────────────────────────────────────────────────
if(MSVC)
    # /Zc:__cplusplus — MSVC otherwise reports __cplusplus as 199711L
    add_compile_options(/Zc:__cplusplus /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── ccache (Linux + macOS; MSVC does not integrate) ───────────────────────────
if(NOT MSVC)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "kirpich: ccache    = ${CCACHE_PROGRAM}")
    endif()
endif()

# ── compile_commands.json for clangd / IDE include resolution ─────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Retro++ engine (git submodule) ────────────────────────────────────────────
# Provides the platform/window/input/run-loop/renderer/audio/VM surface,
# including SDL3 (engine/third_party/sdl) and SameBoy (engine/third_party/sameboy).
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/engine/CMakeLists.txt")
    message(FATAL_ERROR
        "engine/ submodule not initialized. Run: git submodule update --init --recursive")
endif()

# Kirpich asks for SDL's dialog subsystem, which the engine leaves off by default. The
# first-start flow opens a native file picker so the player can point at their own ROM
# (src/assets/first_start.cpp); without this the call reaches a dummy backend and fails.
# Set before add_subdirectory so the engine's non-FORCE default does not win.
set(SDL_DIALOG ON CACHE BOOL "" FORCE)

add_subdirectory(engine)

# ── Dependencies ──────────────────────────────────────────────────────────────
include(cmake/Dependencies.cmake)

# ── Targets ───────────────────────────────────────────────────────────────────
add_subdirectory(src)

# Listening harness for the hosted sound driver — opens a real audio device, so it is built only
# when asked for and never in CI.
option(KIRPICH_BUILD_AUDIO_CHECK "Build the sound-driver listening harness (tools/audio_check)" OFF)
if(KIRPICH_BUILD_AUDIO_CHECK)
    add_subdirectory(tools/audio_check)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
