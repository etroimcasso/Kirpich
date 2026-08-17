// Audio check — play the hosted sound driver through a real audio device and listen to it.
//
// This is a listening harness, not a test. The test suite proves the driver is described correctly
// and that each frame asks for the right things; it cannot tell you whether the result sounds like
// Tetris. This does, by running the real path: it hosts the driver exactly as the game does, then
// drives SoundSystem::tick once per frame at the console's frame rate, cueing through the same
// AudioCues mailbox gameplay writes to.
//
// Audio only — SDL's video subsystem is never initialised, so no window is created.
//
//     audio-check [song] [seconds]
//
//         song     song number, decimal or 0x-prefixed hex (default 5, the Type A theme)
//         seconds  how long to play (default 20)
//
// A few seconds in it fires a rotate effect and a piece-lock effect so the effect mailboxes are
// audible too, and it prints what it is doing as it goes so you can match what you hear to what
// was asked for.

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <retropp/audio.h>

#include <spdlog/spdlog.h>

#include <retropp/asset_registry.h>
#include <retropp/sdl_platform.h>  // retropp::SdlAudioSink

#include "data/music.h"
#include "data/sfx.h"
#include "systems/game_context.h"
#include "systems/sound.h"

namespace {

// The console's true frame period — the rate the driver's per-frame entry is meant to run at.
constexpr double kFrameSeconds = 70224.0 / 4194304.0;

// A sink that opens no device and instead measures what the driver produces: how many frames came
// out and how many of them were not silence. It answers "is this making sound at all" without
// anyone having to listen, which is what separates a dead driver from a wrong read-back.
class MeasuringSink : public retropp::AudioSink {
public:
    ~MeasuringSink() override { stop(); }

    void start(unsigned /*rate*/, int /*channels*/, retropp::AudioPullFn pull) override {
        stop();
        running_ = true;
        thread_  = std::thread([this, pull = std::move(pull)] {
            std::vector<retropp::AudioFrame> buffer(1024);
            while (running_.load(std::memory_order_relaxed)) {
                const std::size_t got = pull(std::span<retropp::AudioFrame>(buffer));
                for (std::size_t i = 0; i < got; ++i) {
                    ++total_;
                    const int left = buffer[i].left < 0 ? -buffer[i].left : buffer[i].left;
                    if (buffer[i].left != 0 || buffer[i].right != 0) {
                        ++nonSilent_;
                        if (firstNonSilent_ == 0) {
                            firstNonSilent_ = total_;
                        }
                        lastNonSilent_ = total_;
                    }
                    if (left > peak_) {
                        peak_ = left;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::size_t total() const { return total_; }
    [[nodiscard]] std::size_t nonSilent() const { return nonSilent_; }
    [[nodiscard]] int         peak() const { return peak_; }
    [[nodiscard]] std::size_t firstNonSilent() const { return firstNonSilent_; }
    [[nodiscard]] std::size_t lastNonSilent() const { return lastNonSilent_; }

private:
    std::atomic<bool> running_{false};
    std::thread       thread_;
    std::size_t       total_          = 0;  // written only on the pull thread, read after join
    std::size_t       nonSilent_      = 0;
    std::size_t       firstNonSilent_ = 0;
    std::size_t       lastNonSilent_  = 0;
    int               peak_           = 0;
};

long parseNumber(const char* text, long fallback) {
    if (text == nullptr) {
        return fallback;
    }
    const std::string value{text};
    try {
        return std::stol(value, nullptr, 0);  // 0 = accept 0x-prefixed hex as well as decimal
    } catch (...) {
        return fallback;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::trace);  // the engine hosts on its own thread; surface its log

    const long song    = parseNumber(argc > 1 ? argv[1] : nullptr, 0x05);
    const long seconds = parseNumber(argc > 2 ? argv[2] : nullptr, 20);

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init(AUDIO) failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    {
#ifdef KIRPICH_PROJECT_ROOT
        // The driver image is read from the project tree, the same place the game reads it.
        retropp::setAssetRoot(std::filesystem::path{KIRPICH_PROJECT_ROOT});
#endif
        const std::filesystem::path image =
            retropp::assetRoot() / "assets/audio/default/sound_driver.bin";
        if (!std::filesystem::exists(image)) {
            std::fprintf(stderr,
                         "The sound driver image is missing:\n  %s\nRun the game once to extract "
                         "it from your ROM, then try again.\n",
                         image.string().c_str());
            SDL_Quit();
            return EXIT_FAILURE;
        }

        // "measure" as a third argument swaps the speakers for a counter — same driver, same path,
        // but it reports whether sound was produced instead of playing it.
        const bool measure = (argc > 3 && std::string{argv[3]} == "measure");

        retropp::SdlAudioSink deviceSink;
        MeasuringSink         measuringSink;
        retropp::AudioSink&   sink =
            measure ? static_cast<retropp::AudioSink&>(measuringSink) : deviceSink;

        kirpich::systems::SoundSystem sound{sink};
        kirpich::systems::GameContext game;

        std::printf("Hosting the sound driver from %s\n", image.string().c_str());
        std::printf("Playing song 0x%02lX for %ld seconds at %.4f Hz.\n\n", song, seconds,
                    1.0 / kFrameSeconds);

        constexpr long kFirstCueFrame = 0;

        const auto frames     = static_cast<long>(static_cast<double>(seconds) / kFrameSeconds);
        const auto rotateAt   = (argc > 4 && std::string{argv[4]} == "lock-only") ? -1 : frames / 4;
        const auto lockAt = frames / 2;
        const auto period     = std::chrono::duration<double>(kFrameSeconds);
        auto       nextFrame  = std::chrono::steady_clock::now();

        for (long frame = 0; frame < frames; ++frame) {
            if (frame == kFirstCueFrame) {
                game.audioCues.music = static_cast<kirpich::MusicId>(song);
                std::printf("  [frame %ld] song 0x%02lX cued\n", frame, song);
            }
            if (frame == rotateAt) {
                game.audioCues.square = kirpich::SquareSfxId::ROTATE_PIECE;
                std::printf("  [frame %ld] rotate effect cued\n", frame);
            }
            if (frame == lockAt) {
                game.audioCues.noise = kirpich::NoiseSfxId::LOCK_PIECE;
                std::printf("  [frame %ld] piece-lock effect cued\n", frame);
            }

            sound.tick(game);

            nextFrame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
            std::this_thread::sleep_until(nextFrame);
        }

        const auto show = [](const char* name, const std::optional<std::uint8_t>& value) {
            if (value.has_value()) {
                std::printf("  %-13s 0x%02X\n", name, static_cast<unsigned>(*value));
            } else {
                std::printf("  %-13s (write-only)\n", name);
            }
        };
        const kirpich::systems::SoundDriverSlots slots = sound.published();
        std::printf("\nWhat the driver publishes back:\n");
        show("pause", slots.pause);
        show("squareSfx", slots.squareSfx);
        show("currentMusic", slots.currentMusic);
        show("waveSfx", slots.waveSfx);
        show("noiseSfx", slots.noiseSfx);
        show("demoNumber", slots.demoNumber);

        std::printf("\nDone. The driver reports it is playing song ");
        const auto playing = sound.currentMusic();
        if (!playing.has_value()) {
            std::printf("nothing — the read-back slot came back disengaged.\n");
            result = EXIT_FAILURE;
        } else if (*playing == kirpich::MusicId::NONE) {
            std::printf("0x00 — it never started the song it was given.\n");
            result = EXIT_FAILURE;
        } else {
            std::printf("0x%02X.\n", static_cast<unsigned>(*playing));
        }

        if (measure) {
            measuringSink.stop();
            std::printf("Produced %zu frames, %zu of them not silence (peak amplitude %d).\n",
                        measuringSink.total(), measuringSink.nonSilent(), measuringSink.peak());
            if (measuringSink.nonSilent() != 0) {
                const auto percent = [&](std::size_t at) {
                    return 100.0 * static_cast<double>(at) / static_cast<double>(measuringSink.total());
                };
                std::printf("Sound ran from frame %zu (%.1f%% in) to %zu (%.1f%% in).\n",
                            measuringSink.firstNonSilent(), percent(measuringSink.firstNonSilent()),
                            measuringSink.lastNonSilent(), percent(measuringSink.lastNonSilent()));
            }
            if (measuringSink.nonSilent() == 0) {
                std::printf("No sound was produced at all.\n");
                result = EXIT_FAILURE;
            }
        }
    }

    SDL_Quit();
    return result;
}
