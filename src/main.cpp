#include <cstdlib>

#include <spdlog/spdlog.h>

#include "retropp/version.h"

#include "engine.h"

int main(int /*argc*/, char* /*argv*/[]) {
    kirpich::Engine engine{};
    (void)engine;

    spdlog::info("kirpich 0.1.0 — Retro++ engine {}", retropp::version());
    return EXIT_SUCCESS;
}
