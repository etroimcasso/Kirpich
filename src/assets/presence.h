#pragma once

#include <string>
#include <vector>

#include <retropp/literal_path.h>

// The required-asset manifest and the startup presence check.
//
// This is NOT a loader. Kirpich loads through the engine's own asset surface
// (retropp::assetPath + the retropp loaders); nothing here decodes an image, touches a
// renderer, or needs a window, so the check runs before anything is initialized.
//
// Kirpich's graphics are derived from a copyrighted ROM and are therefore never committed
// and never shipped. assets/gfx/default/ is an empty directory in the repository; its
// contents arrive by one of two routes that produce an identical layout — the ROM
// extractor running against the user's own ROM, or scripts/setup-dev-assets during
// development. Because both routes write the same files to the same place, this one check
// covers both.

namespace kirpich::assets {

// Every graphic Kirpich requires, addressed as a logical (project-root-relative) path.
// These resolve against retropp::assetRoot() via retropp::assetPath().
//
// They are compile-time literals on purpose: retropp::LiteralPath only constructs from a
// string literal, which keeps the paths greppable in the source and rules out a path built
// from a runtime string.
inline constexpr retropp::LiteralPath kConfigAndGameplay{"assets/gfx/default/configandgameplay.png"};
inline constexpr retropp::LiteralPath kFont{"assets/gfx/default/font.png"};
inline constexpr retropp::LiteralPath kCopyrightAndTitleScreen{"assets/gfx/default/copyrightandtitlescreen.png"};
inline constexpr retropp::LiteralPath kMultiplayerAndBuran{"assets/gfx/default/multiplayerandburan.png"};

// The manifest, in the order missing paths are reported.
inline constexpr retropp::LiteralPath kRequired[]{
    kConfigAndGameplay,
    kFont,
    kCopyrightAndTitleScreen,
    kMultiplayerAndBuran,
};

// What the check found. `missing` holds the logical paths (not the resolved on-disk paths)
// of every required asset that is not present, in manifest order — empty when everything
// is there.
struct PresenceResult {
    std::vector<std::string> missing;

    [[nodiscard]] bool complete() const noexcept { return missing.empty(); }
};

// Check every entry of kRequired against the current retropp::assetRoot(). Performs no
// decode and opens no file — existence only.
[[nodiscard]] PresenceResult checkRequired();

// The message shown when assets are missing. Names the specific missing paths and both
// routes that produce them: the extractor (primary) and manual placement (fallback).
// Kirpich never substitutes placeholder art or ships a fallback — a missing asset is a
// hard, explained stop.
[[nodiscard]] std::string missingAssetsMessage(const PresenceResult& result);

}  // namespace kirpich::assets
