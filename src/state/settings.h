#pragma once

// The player's own options, and the two calls that keep them across launches: how the window is
// opened, which shade ramp the game is drawn in, and whether the falling piece casts a shadow.
//
// None of them belongs to the machine's state image. They survive the reset chord and they outlive a
// launch, so they live outside GameContext and reach the screens through their wiring rather than
// through the game state.
//
// They persist in their own save document, beside the top scores and under the same identity. The
// document is one byte per value, in the order they are declared below. An absent document is an
// ordinary first run and leaves the defaults; a corrupt one is reported, leaves the defaults, and
// leaves the damaged file where it is.
//
// A value outside the range this build offers is clamped rather than refused. A file written by a
// build that offered more sizes should cost the player that one value, not every value beside it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <retropp/save_store.h>

namespace kirpich {

// The window scales the settings screen offers, in screen pixels per Game Boy pixel. The default is
// the engine's own (EnhancementToggles::windowScale), so a player who never opens the screen gets
// the window the engine would have opened anyway.
inline constexpr std::uint8_t kMinWindowScale = 1;
inline constexpr std::uint8_t kMaxWindowScale = 8;
inline constexpr std::uint8_t kDefaultWindowScale = 4;

struct Settings {
    bool         fullscreen  = false;
    std::uint8_t windowScale = kDefaultWindowScale;

    // Which shade ramp the game is drawn in (src/render/palettes.h). 0 is the greyscale the hardware's
    // own shades map to, so a player who never touches it sees what they always saw.
    std::uint8_t shadeRamp = 0;

    // Whether the falling piece casts a shadow on the row it would land on (src/render/ghost_piece.h).
    // Off by default: it is help the original does not give, so a player gets the game they remember
    // until they ask for something else.
    bool ghostPiece = false;

    // Whether the game types the cartridge never had are offered. Off by default, so the config screen
    // shows the two choices it always showed until a player asks for more.
    bool newModes = false;

    // Whether the cartridge's audio quirk is fixed: its sound driver mutes everything while the
    // demo-number byte is set, nothing clears that byte when an attract demo ends, and the title
    // screen re-cues its song while it is still set - so after one demo the title stays silent for
    // the rest of the session, on hardware and here alike. Off by default: the quirk is the
    // cartridge's own behavior, and a player gets it until they ask for the fix (the fixes screen,
    // settings page two).
    bool fixAudio = false;

    friend bool operator==(const Settings&, const Settings&) = default;
};

// The settings save document: schema version and the image size. The name is spelled as a literal at
// the call sites, as the top-score document's is.
//
// One byte per value, so every version appends to the one before it: version 2 adds the ghost-piece
// flag to version 1's three bytes, version 3 adds the new-modes flag to version 2's four, and
// version 4 adds the audio-fix flag to version 3's five. An older document is migrated on the way
// in, a step at a time, not read short: two formats answering to one version number is what a schema
// version exists to prevent.
//
// A shorter image is still read as far as it goes and the values it does not carry keep their
// defaults, which keeps a truncated file costing one setting rather than all of them. A longer image
// is refused, since nothing can be said about bytes this build does not understand.
inline constexpr std::uint32_t kSettingsSchemaVersion = 4;
inline constexpr std::size_t   kSettingsImageBytes    = 6;

// What each earlier version wrote. Named so each migration and its test say the same number.
inline constexpr std::size_t kSettingsImageBytesV1 = 3;  // the flag, the scale, the ramp
inline constexpr std::size_t kSettingsImageBytesV2 = 4;  // and the ghost-piece flag
inline constexpr std::size_t kSettingsImageBytesV3 = 5;  // and the new-modes flag

// Bring a scale into the range this build offers. Values below the floor come up to it and values
// above the ceiling come down to it; the range itself is what a build changes when it offers more.
[[nodiscard]] std::uint8_t clampWindowScale(int scale);

// Encode the settings into the image: the fullscreen flag as 0 or 1, then the scale, then the ramp,
// then the ghost-piece flag, then the new-modes flag, then the audio-fix flag.
[[nodiscard]] std::array<std::uint8_t, kSettingsImageBytes> encodeSettings(const Settings& settings);

// Decode an image into `settings`. Returns false and leaves `settings` untouched when the image is
// empty or longer than this build writes; true otherwise, with any byte the image does not carry
// left at its default. Any non-zero flag byte means on, and both the scale and the ramp are clamped
// on the way in.
[[nodiscard]] bool decodeSettings(std::span<const std::uint8_t> image, Settings& settings);

// Bring a version 1 image up to version 2 by appending the ghost-piece flag, off; a version 2 image
// up to version 3 by appending the new-modes flag, off; and a version 3 image up to version 4 by
// appending the audio-fix flag, off. A document written before a setting existed cannot say anything
// about it, and off is what it would have been.
//
// The store runs them in sequence, so a version 1 document reaches version 4 through all three.
//
// Exposed so each migration can be tested for what it does rather than only through a store.
[[nodiscard]] std::vector<std::byte> migrateSettingsV1ToV2(std::vector<std::byte> payload);
[[nodiscard]] std::vector<std::byte> migrateSettingsV2ToV3(std::vector<std::byte> payload);
[[nodiscard]] std::vector<std::byte> migrateSettingsV3ToV4(std::vector<std::byte> payload);

// Persist the settings as document "settings" at the current schema version. Returns whatever the
// atomic write reports.
bool saveSettings(const Settings& settings, retropp::SaveStore& store);

// Load the settings from the store, migrating an older document forward on the way in. Absent
// document (ordinary first run) -> leave the defaults, return false. Present and valid -> decode,
// return true. Corrupt or wrong length -> log an error, leave the defaults, leave the damaged file
// in place, return false.
//
// Declares this document's schema version and migration chain on the store before reading, because
// both are the store's own rather than per-document: every loader sharing a store must name its own
// version immediately before its own read (src/state/high_score_persistence.h does the same).
bool loadSettings(retropp::SaveStore& store, Settings& settings);

}  // namespace kirpich
