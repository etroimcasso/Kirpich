#include "assets/first_start.h"

#include <atomic>
#include <mutex>

#include "assets/presence.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

namespace kirpich::assets {
namespace {

// What the dialog callback hands back to the waiting main thread. SDL may invoke the
// callback on another thread, so the path is written under the mutex and `finished` is the
// release/acquire handshake the wait loop spins on.
struct DialogOutcome {
    std::mutex             lock;
    std::string            path;
    std::atomic<bool>      finished{false};
};

void onFileChosen(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* outcome = static_cast<DialogOutcome*>(userdata);

    // filelist == nullptr is an error; a pointer to nullptr means the player cancelled.
    // Both leave `path` empty, which the caller reads as "no ROM chosen".
    if (filelist != nullptr && filelist[0] != nullptr) {
        const std::lock_guard guard{outcome->lock};
        outcome->path = filelist[0];
    }
    outcome->finished.store(true, std::memory_order_release);
}

}  // namespace

std::optional<std::filesystem::path> promptForRom() {
    // The dialog is a platform window, so the video subsystem has to be up. Kirpich has no
    // window of its own yet at this point in startup — the dialog is parentless, which
    // every supported platform allows.
    const bool videoWasUp = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    if (!videoWasUp && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return std::nullopt;
    }

    const SDL_DialogFileFilter filters[]{
        {"Game Boy ROM", "gb"},
        {"All files", "*"},
    };

    DialogOutcome outcome;
    SDL_ShowOpenFileDialog(onFileChosen, &outcome, /*window=*/nullptr, filters,
                           static_cast<int>(std::size(filters)), /*default_location=*/nullptr,
                           /*allow_many=*/false);

    // Block until the callback fires. Pumping events is required, not merely polite: the
    // portal-based dialogs on Linux run over DBus and never complete without it.
    while (!outcome.finished.load(std::memory_order_acquire)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }

    if (!videoWasUp) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    const std::lock_guard guard{outcome.lock};
    if (outcome.path.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path{outcome.path};
}

ExtractionResult extractFromRom(const std::filesystem::path& romPath) {
    // The flow around this call is complete; the extractor itself is scheduled work. It
    // reports honestly rather than pretending to have written anything, so a player never
    // sees a success message followed by a failure to load.
    return {
        .succeeded = false,
        .message   = "Kirpich cannot extract graphics yet — the extractor is still being built.\n"
                     "\n"
                     "The ROM you chose (" + romPath.string() + ") was not read, and no files were\n"
                     "written. Until the extractor lands, the graphics have to be placed by hand at\n"
                     "the paths listed above, relative to the asset root; the layout is documented in\n"
                     "docs/features/asset-acquisition.md.",
    };
}

bool ensureAssetsPresent(const std::function<void(const std::string&)>& report) {
    PresenceResult presence = checkRequired();
    if (presence.complete()) {
        return true;
    }

    report(missingAssetsMessage(presence));

    const std::optional<std::filesystem::path> rom = promptForRom();
    if (!rom) {
        report("No ROM was chosen, so there is nothing to extract from. Kirpich cannot start.");
        return false;
    }

    const ExtractionResult extraction = extractFromRom(*rom);
    report(extraction.message);
    if (!extraction.succeeded) {
        return false;
    }

    // Re-check rather than trusting the extractor's word: the load path that follows only
    // works if the files are actually on disk.
    presence = checkRequired();
    if (!presence.complete()) {
        report(missingAssetsMessage(presence));
        return false;
    }
    return true;
}

}  // namespace kirpich::assets
