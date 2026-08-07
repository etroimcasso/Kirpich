#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

// The first-start flow: what happens when Kirpich launches and its graphics are not there.
//
// Kirpich ships with no graphics — they are derived from the Game Boy Tetris ROM. Rather
// than failing with an error that sends the player off to run a command-line tool, the
// port asks for the ROM on the spot: a native file-selection dialog, extraction in-process,
// and then the ordinary load path. The same first-start model as Ship of Harkinian.
//
// The sequence lives entirely in the port, ahead of engine construction:
//
//   1. checkRequired() (presence.h) — are the graphics there?
//   2. If not, promptForRom() — the player points at their own ROM.
//   3. extractFromRom() (extract.h) — writes every required file into assets/gfx/default/.
//   4. Proceed into normal engine construction and asset loading — the same code path
//      every subsequent launch takes.
//
// No pre-asset engine state, no restart, and nothing the engine has to know about.

namespace kirpich::assets {

// Show the platform's native file-selection dialog and block until the player chooses a
// ROM or dismisses it. Returns the chosen path, or nullopt if they cancelled or the
// dialog could not be shown.
//
// Must be called from the main thread. Initializes SDL's video subsystem if it is not
// already up (the dialog is a platform window) and pumps events while it waits, which is
// what lets the portal-based dialogs on Linux complete.
[[nodiscard]] std::optional<std::filesystem::path> promptForRom();

// Run the whole sequence: check, and if anything is missing, prompt and extract. Returns
// true when the graphics are present afterwards and startup may continue.
//
// `report` receives player-facing text — one line or many — for whatever the caller wants
// to do with it (stderr at startup today, an on-screen panel once there is a window).
[[nodiscard]] bool ensureAssetsPresent(const std::function<void(const std::string&)>& report);

}  // namespace kirpich::assets
