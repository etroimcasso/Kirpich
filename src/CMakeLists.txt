# Port library — main.cpp excluded so tests can link the lib without
# pulling in the program entry point. Polyrhythm (retropp::engine) is the
# platform boundary; it is linked PUBLIC so the binary and tests see the
# retropp:: surface transitively.
add_library(kirpich-lib STATIC
    assets/asset_root.cpp
    assets/presence.cpp
    assets/first_start.cpp
    assets/extract.cpp
    assets/png_writer.cpp
    data/charmap.cpp
    data/gravity.cpp
    data/scoring.cpp
    state/high_score_persistence.cpp
    state/settings.cpp
    state/stats_persistence.cpp
    systems/input.cpp
    systems/game_state_dispatcher.cpp
    systems/boot.cpp
    systems/piece.cpp
    systems/line_clear.cpp
    systems/scoring.cpp
    systems/menu_screens.cpp
    systems/mode_screen.cpp
    systems/carousel_screen.cpp
    systems/enhancement_screens.cpp
    systems/title_screens.cpp
    systems/gameplay.cpp
    systems/type_b_ending.cpp
    systems/launch_scenes.cpp
    systems/high_scores.cpp
    systems/demo.cpp
    systems/readouts.cpp
    systems/rising_floor.cpp
    systems/screen.cpp
    systems/settings_screen.cpp
    systems/sound.cpp
    systems/sprite_renderer.cpp
    systems/stats.cpp
    render/tile_atlas.cpp
    render/background.cpp
    render/sprites.cpp
    render/settings_overlay.cpp
    render/config_section.cpp
    render/ghost_piece.cpp
    render/type_c_difficulty.cpp
    vm/piece_random.cpp
    vm/garbage_fill.cpp
)
target_include_directories(kirpich-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Development builds resolve the asset root to the project tree, so a developer who has run
# scripts/setup-dev-assets exercises the shipped load path against the real files without
# copying anything next to the binary. A distributable turns this off, and assets resolve to the
# player's own data directory — the same place their save file goes, which is where the extractor
# writes them.
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
# The routine bake above generates a registry translation unit that nothing references. The engine
# anchors it with an undefined-symbol link option of its own, so an ordinary link keeps it and the
# baked routines ship in the binary. Symbol check: `nm` on the built executable (`build/Kirpich`, or
# `build/Kirpich.app/Contents/MacOS/Kirpich` on macOS) piped through `grep retropp_routine_` must
# not be empty.
add_executable(kirpich main.cpp)
target_link_libraries(kirpich PRIVATE kirpich-lib)
target_compile_features(kirpich PRIVATE cxx_std_20)
target_compile_definitions(kirpich PRIVATE KIRPICH_VERSION="${PROJECT_VERSION}")

# The name a player sees. The CMake target stays lowercase because that is what the build refers to
# everywhere; the artifact is a proper noun.
set_target_properties(kirpich PROPERTIES OUTPUT_NAME "Kirpich")

# ── A game, not a command-line program ────────────────────────────────────────
# Both desktop platforms decide what an executable IS from how it is built, and the default on each
# is "a terminal program". Double-clicked, a bare Mach-O is handed to Terminal.app, and a
# console-subsystem Windows binary is given a console window whether it wants one or not. Neither is
# anything to do with the game's own output — they happen before main runs.
if(APPLE)
    # An .app is what the Finder launches directly, what a signature and a notarization ticket
    # attach to, and what carries the identifier and version. The executable inside it keeps the
    # artifact name, so the bundle is Kirpich.app/Contents/MacOS/Kirpich.
    set_target_properties(kirpich PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/KirpichBundleInfo.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Kirpich"
        MACOSX_BUNDLE_EXECUTABLE_NAME "Kirpich"
        # Chosen once and never changed: the player's saves and extracted assets are found under
        # this identity, and a notarization ticket is bound to it. Changing it strands both.
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.kirpich.kirpich"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "GNU Affero General Public License v3.0")
elseif(WIN32)
    # WIN32_EXECUTABLE links against the windows subsystem, which is what stops the console window
    # from appearing. It normally also demands WinMain — but the engine defines SDL_MAIN_HANDLED and
    # calls SDL_SetMainReady() itself, so the game keeps a plain main() and the entry point is named
    # explicitly rather than pulling in SDL's main shim to bridge the two.
    set_target_properties(kirpich PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(kirpich PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

# The shipped binary carries only what it runs.
#
# The build type is already Release; these are the other two halves of a lean artifact — the linker
# drops what nothing reaches, and the symbol table nobody needs at runtime comes off. A Debug build
# keeps all of it: this is what ships, not what is debugged.
#
# It has to coexist with the routine bake above, and does. Each generated registry is anchored by an
# extern "C" symbol the engine names undefined at link time, so the anchor is a root the dead strip
# keeps, and it is a global symbol that `strip -x` does not touch. The checks are the ones that were
# already there: the same `nm` check as above must not be empty, and tests/test_embedded_routines is
# the guard that the routines still register.
#
# MSVC is left alone — its Release link already drops unreferenced code (/OPT:REF /OPT:ICF) and puts
# symbols in a separate .pdb rather than in the executable.
if(NOT MSVC)
    if(APPLE)
        # ld64 reaches unreferenced code without per-function sections; the other linkers need to be
        # given the granularity first, which is why the flag pair below is compiled into the library.
        target_link_options(kirpich PRIVATE $<$<NOT:$<CONFIG:Debug>>:-Wl,-dead_strip>)
    else()
        target_compile_options(kirpich-lib PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-ffunction-sections>
            $<$<NOT:$<CONFIG:Debug>>:-fdata-sections>)
        target_compile_options(kirpich PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-ffunction-sections>
            $<$<NOT:$<CONFIG:Debug>>:-fdata-sections>)
        target_link_options(kirpich PRIVATE $<$<NOT:$<CONFIG:Debug>>:-Wl,--gc-sections>)
    endif()

    # Apple's -x keeps the global symbols and drops the local ones, which is what leaves the routine
    # anchors in place; elsewhere an executable's whole symbol table goes.
    if(CMAKE_STRIP AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        if(APPLE)
            set(kirpich_strip_options -x)
        else()
            set(kirpich_strip_options "")
        endif()
        add_custom_command(TARGET kirpich POST_BUILD
            COMMAND "${CMAKE_STRIP}" ${kirpich_strip_options} "$<TARGET_FILE:kirpich>"
            COMMENT "Stripping symbols from the shipped binary")
    endif()
endif()

# Sign the bundle, last, after everything that modifies it.
#
# Apple silicon will not run an unsigned binary at all, so the linker ad-hoc signs every build. The
# strip above edits the binary afterwards, which invalidates that signature — and a bundle's
# signature has to cover its Info.plist and seal its resources besides, which a linker signature over
# the executable alone never does. Left that way macOS calls the result damaged rather than
# unsigned, and refuses to open it at all: a broken signature is not something a player can wave
# past, where an absent one is.
#
# Ad-hoc means a signature with no identity behind it: enough to run, not enough for someone else's
# machine to trust a download. Signing for distribution replaces this and belongs to the release.
if(APPLE)
    add_custom_command(TARGET kirpich POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:kirpich>"
        COMMAND codesign --verify --strict "$<TARGET_BUNDLE_DIR:kirpich>"
        COMMENT "Signing the app bundle")
endif()
