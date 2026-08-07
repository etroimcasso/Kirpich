#include "assets/first_start.h"

#include <atomic>
#include <mutex>

#include "assets/extract.h"
#include "assets/presence.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>

#include <spdlog/spdlog.h>

namespace kirpich::assets {
namespace {

// Which of the three ways the dialog can end actually happened. SDL reports an error and a
// cancellation through the same callback, distinguished only by whether `filelist` is null,
// and conflating them tells a player who hit a broken dialog that they declined to pick a
// file.
enum class DialogEnd { Chosen, Cancelled, Failed };

// What the dialog callback hands back to the waiting main thread. SDL may invoke the
// callback on another thread, so the path is written under the mutex and `finished` is the
// release/acquire handshake the wait loop spins on.
struct DialogOutcome {
    std::mutex            lock;
    std::string           path;
    DialogEnd             end = DialogEnd::Cancelled;
    std::string           error;
    std::atomic<bool>     finished{false};
};

void onFileChosen(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* outcome = static_cast<DialogOutcome*>(userdata);
    {
        const std::lock_guard guard{outcome->lock};
        if (filelist == nullptr) {
            // The dialog itself failed. SDL_GetError is only meaningful here.
            outcome->end   = DialogEnd::Failed;
            const char* why = SDL_GetError();
            outcome->error  = (why != nullptr) ? why : "";
        } else if (filelist[0] == nullptr) {
            outcome->end = DialogEnd::Cancelled;
        } else {
            outcome->end  = DialogEnd::Chosen;
            outcome->path = filelist[0];
        }
    }
    outcome->finished.store(true, std::memory_order_release);
}

}  // namespace

std::optional<std::filesystem::path> promptForRom() {
    // The dialog is a platform window, so the video subsystem has to be up. Kirpich has no
    // window of its own yet at this point in startup — the dialog is parentless, which
    // every supported platform allows.
    // SDL refcounts subsystem initialization, so asking for video here is safe whether or not
    // it is already up. It is deliberately NOT torn down afterwards: the panel's completion
    // handler is still unwinding when the callback returns, and quitting the subsystem out
    // from under it hangs the process on macOS. Video is the engine's from here on anyway.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        spdlog::error("Could not start SDL video, so no file dialog can be shown: {}",
                      SDL_GetError());
        return std::nullopt;
    }

    const SDL_DialogFileFilter filters[]{
        {"Game Boy ROM", "gb"},
        {"All files", "*"},
    };

    // The dialog says what it is FOR, not just "Open". The plain SDL_ShowOpenFileDialog
    // carries no title, so the properties form of the same dialog is used; a platform that
    // cannot display a title shows its stock chrome, which is the same dialog minus the
    // words.
    DialogOutcome          outcome;
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                           const_cast<SDL_DialogFileFilter*>(static_cast<const SDL_DialogFileFilter*>(filters)));
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                          static_cast<Sint64>(std::size(filters)));
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                          "Locate your Game Boy Tetris ROM");
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, onFileChosen, &outcome, props);

    // Block until the callback fires. Pumping events is required, not merely polite: the
    // portal-based dialogs on Linux run over DBus and never complete without it.
    while (!outcome.finished.load(std::memory_order_acquire)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);

    const std::lock_guard guard{outcome.lock};
    switch (outcome.end) {
        case DialogEnd::Chosen:
            return std::filesystem::path{outcome.path};
        case DialogEnd::Cancelled:
            return std::nullopt;
        case DialogEnd::Failed:
            spdlog::error("The file dialog could not be opened: {}",
                          outcome.error.empty() ? "no reason given" : outcome.error);
            return std::nullopt;
    }
    return std::nullopt;
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
