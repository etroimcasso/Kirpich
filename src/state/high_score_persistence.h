#pragma once

// Top-score persistence: the wire codec that turns HighScoreState's two tables into the exact
// 1890-byte ROM image (and back), and the two calls that carry that image through the engine's
// durable SaveStore. This is the port's first durable state.
//
// The original keeps top scores in work RAM only: a hard boot zeroes $D000-$DFFF, so a power cycle
// loses them, while a soft reset deliberately skips that clear so scores survive it and nothing else.
// The port persists them across launches - always on, no toggle - which strictly extends the
// original's soft-reset survival; the in-sim table behaviour is unchanged. The engine owns the
// storage primitive (retropp::SaveStore: named, schema-versioned, atomic byte documents in a
// per-user directory), so the port adds only this codec and the two calls; no file I/O is invented
// port-side. Wiring the calls into the game loop (load at startup, save on name-entry submit) is
// later game-flow work - nothing can earn a score before the loop exists.
//
// The payload IS the ROM wire image: the wTypeBTopScores block (1620 bytes) followed by the
// wTypeATopScores block (270 bytes), in address order - BCD scores low-pair-first, six charmap name
// bytes per entry, exactly the WRAM layout docs/contracts/high-score-state.md pins. Reusing the ROM
// layout avoids minting a second format for data whose layout is already contract-pinned.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <retropp/save_store.h>

#include "state/high_score_state.h"

namespace kirpich {

// The save identity - the per-user save directory is <platform data dir>/Kirpich/Kirpich/. Locked
// permanently (a changed identity strands players' existing documents under the old directory); wired
// into EngineConfig at startup (later game-flow work). These are identity facts, not engine-ingested
// asset paths, so
// named constants are correct here.
inline constexpr std::string_view kSaveOrganization = "Kirpich";
inline constexpr std::string_view kSaveApplication = "Kirpich";

// The top-score save document: name (a flat identifier, spelled as a literal at the call sites too),
// schema version, and the fixed wire-image size.
inline constexpr std::string_view kTopScoresDocument = "topscores";
inline constexpr std::uint32_t kTopScoresSchemaVersion = 1;
inline constexpr std::size_t kTopScoresImageBytes = 1890;  // 1620 (Type B) + 270 (Type A)

// Encode the two tables into the 1890-byte ROM wire image: the Type B block then the Type A block, in
// address order. Only the tables are serialised - the HRAM session fields (newTopScore, rank, column,
// redraw) never persist.
std::array<std::uint8_t, kTopScoresImageBytes> encodeTopScores(const HighScoreState& state);

// Decode a wire image into `state`'s two tables. Returns false and leaves `state` untouched when the
// image is not exactly 1890 bytes; true on success. Writes only the tables.
bool decodeTopScores(std::span<const std::uint8_t> image, HighScoreState& state);

// Persist the two tables to the store as document "topscores" version 1. Returns whatever the atomic
// write reports (true when the document is durably on disk).
bool saveTopScores(const HighScoreState& state, retropp::SaveStore& store);

// Load the two tables from the store into `state`. Absent document (ordinary first run) -> leave the
// boot zeros, return false. Present and valid -> decode into the tables, return true. Corrupt
// (SaveStoreError) or wrong length -> log an error, leave the boot zeros, leave the damaged file in
// place (never treated as absent, never proactively overwritten), return false. The HRAM session
// fields are never touched.
bool loadTopScores(retropp::SaveStore& store, HighScoreState& state);

}  // namespace kirpich
