#pragma once

// Top-score persistence: the wire codec that turns HighScoreState's three tables into one flat byte
// image (and back), and the two calls that carry that image through the engine's durable SaveStore.
// This is the port's first durable state.
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
// The payload opens with the ROM wire image: the wTypeBTopScores block (1620 bytes) followed by the
// wTypeATopScores block (270 bytes), in address order - BCD scores low-pair-first, six charmap name
// bytes per entry, exactly the WRAM layout docs/contracts/high-score-state.md pins. Reusing the ROM
// layout avoids minting a second format for data whose layout is already contract-pinned. Type C's
// block follows, in the same slice format; the cartridge has no address for it, so it has no address
// order to honour and simply comes last.

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
//
// Version 2 appended Type C's table to the two the cartridge has. Version 3 widens that table, because
// a Type C round is now picked as a level and a rise and each pair keeps its own scores. An older
// document is migrated on the way in - v1 through migrateTopScoresV1ToV2, then v2 through
// migrateTopScoresV2ToV3 - rather than read short, so one version number never means two formats.
inline constexpr std::string_view kTopScoresDocument = "topscores";
inline constexpr std::uint32_t kTopScoresSchemaVersion = 3;

// What one (level, rise) or (level, height) slice costs on the wire: three BCD scores then three
// six-glyph names.
inline constexpr std::size_t kTopScoresSliceBytes = 27;

// The Type C block's two dimensions, which the migration walks and HighScoreState::typeC declares.
inline constexpr std::size_t kTopScoresTypeCLevels = 10;
inline constexpr std::size_t kTopScoresTypeCRises  = 6;

// What each block costs on the wire, and what a whole document costs at each version.
inline constexpr std::size_t kTopScoresTypeBBytes = 1620;  // 10 levels x 6 heights x 3 ranks
inline constexpr std::size_t kTopScoresTypeABytes = 270;   // 10 levels x 3 ranks
inline constexpr std::size_t kTopScoresTypeCBytesV2 = 270;   // Type A's shape, when a rise was fixed
inline constexpr std::size_t kTopScoresTypeCBytes = 1620;    // Type B's shape: 10 levels x 6 rises
inline constexpr std::size_t kTopScoresImageBytesV1 =
    kTopScoresTypeBBytes + kTopScoresTypeABytes;  // 1890
inline constexpr std::size_t kTopScoresImageBytesV2 =
    kTopScoresImageBytesV1 + kTopScoresTypeCBytesV2;  // 2160
inline constexpr std::size_t kTopScoresImageBytes =
    kTopScoresImageBytesV1 + kTopScoresTypeCBytes;  // 3510

// Encode the three tables into the wire image: the Type B block, then the Type A block - the two in
// the cartridge's own address order - then Type C's, level-major with the six rises of a level
// consecutive. Only the tables are serialised; the HRAM session fields (newTopScore, rank, column,
// redraw) never persist.
std::array<std::uint8_t, kTopScoresImageBytes> encodeTopScores(const HighScoreState& state);

// Decode a wire image into `state`'s three tables. Returns false and leaves `state` untouched when the
// image is not exactly kTopScoresImageBytes; true on success. Writes only the tables.
bool decodeTopScores(std::span<const std::uint8_t> image, HighScoreState& state);

// Bring a version 1 image up to version 2 by appending an empty Type C block. A document written
// before the mode existed cannot carry any scores for it, and no scores is what it would have had.
//
// Exposed so the migration can be tested for what it does rather than only through a store.
[[nodiscard]] std::vector<std::byte> migrateTopScoresV1ToV2(std::vector<std::byte> payload);

// Which rise slot a version 2 Type C score belongs in. Every Type C round played before this version
// ran at a rise of 10, because that was the only rise there was, so that is where those scores go.
//
// The value this indexes lives in kTypeCRiseValues (src/systems/rising_floor.h), which a state header
// must not include - the tie is asserted in tests/test_high_score_state.cpp instead, so a reordered
// value table cannot quietly change what this migration means.
inline constexpr std::size_t kTopScoresMigratedRiseIndex = 3;

// Bring a version 2 image up to version 3 by widening its Type C block from one slice per level to six.
// Each level's existing slice is placed at kTopScoresMigratedRiseIndex and the level's other five rises
// start empty; the Type B and Type A blocks are carried through untouched.
//
// Exposed so the migration can be tested for what it does rather than only through a store.
[[nodiscard]] std::vector<std::byte> migrateTopScoresV2ToV3(std::vector<std::byte> payload);

// Persist the three tables to the store as document "topscores" at the current schema version.
// Returns whatever the atomic write reports (true when the document is durably on disk).
bool saveTopScores(const HighScoreState& state, retropp::SaveStore& store);

// Load the tables from the store into `state`, migrating an older document forward on the way in.
// Absent document (ordinary first run) -> leave the boot zeros, return false. Present and valid ->
// decode into the tables, return true. Corrupt (SaveStoreError) or wrong length -> log an error, leave
// the boot zeros, leave the damaged file in place (never treated as absent, never proactively
// overwritten), return false. The HRAM session fields are never touched.
bool loadTopScores(retropp::SaveStore& store, HighScoreState& state);

}  // namespace kirpich
