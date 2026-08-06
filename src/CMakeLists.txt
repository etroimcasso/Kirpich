# Port library — main.cpp excluded so tests can link the lib without
# pulling in the program entry point. Retro++ (retropp::engine) is the
# platform boundary; it is linked PUBLIC so the binary and tests see the
# retropp:: surface transitively.
add_library(kirpich-lib STATIC
    engine.cpp
    assets/presence.cpp
    assets/first_start.cpp
    data/charmap.cpp
    data/gravity.cpp
)
target_include_directories(kirpich-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

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

# Port binary
add_executable(kirpich main.cpp)
target_link_libraries(kirpich PRIVATE kirpich-lib)
target_compile_features(kirpich PRIVATE cxx_std_20)
