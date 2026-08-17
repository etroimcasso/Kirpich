#pragma once

#include <string>
#include <vector>

// The startup presence check for the files Kirpich requires: the game's graphics and the
// sound driver's image.
//
// This is NOT a loader. Kirpich loads through the engine's own asset surface; nothing here
// decodes an image, places a driver, touches a renderer, or needs a window, so the check runs
// before anything is constructed.
//
// There is deliberately no path constant in this header — no manifest array, no named
// kFont. Asset paths are never stored in variables, in any form: the engine's build scan
// reads path literals textually out of the source, so a path that lives in a named binding
// is invisible to it, and a constant here would invite a future load site to reference it.
// Every required path is written out as a literal inside checkRequired()
// (src/assets/presence.cpp), and every future load site spells its own literal in place.
//
// Kirpich's graphics and its sound driver image are both derived from a copyrighted ROM and
// are never committed or shipped. assets/gfx/default/ and assets/audio/default/ are empty in
// the repository; their contents arrive from the first-start ROM extraction or from
// scripts/setup-dev-assets during development. Both routes write the same files to the same
// places, so this one check covers both.

namespace kirpich::assets {

// What the check found. `missing` holds the logical (project-root-relative) paths of every
// required asset that is not present, in reporting order — empty when everything is there.
struct PresenceResult {
    std::vector<std::string> missing;

    [[nodiscard]] bool complete() const noexcept { return missing.empty(); }
};

// Check every required graphic against the current retropp::assetRoot(). Performs no
// decode and opens no file — existence only.
[[nodiscard]] PresenceResult checkRequired();

// The message shown when assets are missing. Names the specific missing paths and explains
// that Kirpich will now ask for the ROM. Never a bare error pointing at a tool the player
// must go run; never placeholder art; never a bundled fallback.
[[nodiscard]] std::string missingAssetsMessage(const PresenceResult& result);

}  // namespace kirpich::assets
