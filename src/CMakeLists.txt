# Port library — main.cpp excluded so tests can link the lib without
# pulling in the program entry point. Retro++ (retropp::engine) is the
# platform boundary; it is linked PUBLIC so the binary and tests see the
# retropp:: surface transitively.
add_library(kirpich-lib STATIC
    engine.cpp
    assets/asset_root.cpp
    assets/presence.cpp
    assets/first_start.cpp
    assets/extract.cpp
    assets/png_writer.cpp
    data/charmap.cpp
    data/gravity.cpp
    data/scoring.cpp
    state/high_score_persistence.cpp
    systems/input.cpp
    systems/game_state_dispatcher.cpp
    systems/piece.cpp
    systems/line_clear.cpp
    systems/scoring.cpp
    systems/menu_screens.cpp
    systems/title_screens.cpp
    systems/gameplay.cpp
    systems/type_b_ending.cpp
    systems/screen.cpp
    systems/sound.cpp
    systems/sprite_renderer.cpp
    render/tile_atlas.cpp
    render/background.cpp
    render/sprites.cpp
    vm/piece_random.cpp
    vm/garbage_fill.cpp
)
target_include_directories(kirpich-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# How a consumer must link kirpich-lib to get the baked routines — see the note on the executable
# below. Exported so the test binary links it the same way the shipped binary does; a suite that
# linked differently could not catch a linkage defect in the thing it is testing.
#
# INTERIM, with the executable's wrapper. Remove both when the engine keeps the guarantee itself.
set(KIRPICH_LIB_WHOLE_ARCHIVE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kirpich-lib>" CACHE INTERNAL "")

# Development builds resolve the asset root to the project tree, so a developer who has run
# scripts/setup-dev-assets exercises the shipped load path against the real files without
# copying anything next to the binary. A distributable turns this off and the engine's
# default applies instead — the executable's own directory, where extracted assets land.
option(KIRPICH_DEV_ASSET_ROOT "Resolve the asset root to the project tree (development builds)" ON)
if(KIRPICH_DEV_ASSET_ROOT)
    target_compile_definitions(kirpich-lib PUBLIC KIRPICH_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
endif()
target_link_libraries(kirpich-lib PUBLIC
    retropp::engine
    spdlog::spdlog_header_only
)
target_compile_features(kirpich-lib PUBLIC cxx_std_20)

# The piece randomizer's draw core is an SM83 routine registered from src/vm/random.asm
# (src/vm/piece_random.cpp). This scans the target's sources for that registration, assembles the
# .asm at build time, and bakes the bytecode into a generated registry so the routine ships in the
# binary with no runtime .asm file. Called explicitly because kirpich-lib is defined in this
# subdirectory, which the engine's own auto-scan (retropp_finalize_assets, top-level only) does not
# reach.
retropp_autoembed_routines(kirpich-lib)

# Port binary
#
# kirpich-lib is whole-archived. The routine bake (retropp_autoembed_routines above) generates a
# translation unit whose only content is an anonymous-namespace static initializer; nothing
# references it, so when the archive is linked normally the linker discards that object and every
# baked routine with it. The binary then falls through to reading the .asm from the asset root,
# which exists only inside a source tree — so a build run anywhere else aborts on the first
# registerRoutine call. Whole-archiving keeps the registry object, which is what makes the embed
# real. Symbol check: `nm build/kirpich | grep retropp_routine_` must not be empty.
#
# INTERIM. This is an engine mechanism and the engine should keep its own guarantee; the request is
# filed. Remove this wrapper when it lands.
add_executable(kirpich main.cpp)
target_link_libraries(kirpich PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kirpich-lib>")
target_compile_features(kirpich PRIVATE cxx_std_20)
