include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# SDL3 is NOT declared here. It arrives transitively from the Retro++ engine
# (engine/third_party/sdl); a second provider of the SDL3::SDL3 target is a
# configure-time error.

# ── spdlog ────────────────────────────────────────────────────────────────────
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ── GoogleTest ────────────────────────────────────────────────────────────────
set(BUILD_GMOCK            OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)  # Windows MSVC compatibility

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)
