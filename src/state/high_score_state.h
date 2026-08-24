#pragma once

// The high-score state: every byte the top-score machinery persists across frames, expressed as one
// plain C++ struct. Where EngineState (src/state/engine_state.h) holds the $C000 gameplay globals,
// GameFlowState (src/state/game_flow_state.h) holds the main-loop bookkeeping, and the other state
// blocks hold the sprite/serial/demo bytes, HighScoreState holds the two work-RAM top-score
// tables ($D000 wTypeBTopScores, $D654 wTypeATopScores) plus the four high-RAM bytes the score-entry
// flow reads and writes: whether a new top score was just earned, whether the staged rows need a
// VBlank redraw, which rank the new score took, and which name-entry column the cursor sits on.
//
// The struct is idiomatic, not a byte image of RAM. Each stored score is a decimal integer, not the
// ROM's three packed-decimal (BCD) bytes; each name is six CharTile glyphs, matching the ROM's six
// name bytes. The two tables are plain nested arrays indexed [level][height][rank] (Type B) and
// [level][rank] (Type A). The BCD<->decimal conversion and the exact 1890-byte ROM wire image live in
// the persistence pair (src/state/high_score_persistence.{h,cpp}), not here. The exact byte-by-byte
// mapping back to the RAM addresses - the slice layout, the rank inversion, the name-entry column
// split with GameFlowState, the derived name-cursor pointer, and the upstream quirks this surface
// preserves - is written up in docs/contracts/high-score-state.md.
//
// Every member is zero-initialised, so a default-constructed HighScoreState is the boot state (the
// original's hard boot zeroes $D000-$DFFF; a soft reset deliberately skips that clear so scores
// survive, the original's only persistence - see the contract). reset() returns a live instance to
// boot. It is a sibling of the other state structs, not a member of any; aggregating the state blocks
// into the running game is later wiring.

#include <array>
#include <cstdint>

#include <kirpich/char_tile.h>

namespace kirpich {

// One top-score entry: a decimal score and a six-glyph name. Three entries share each slice of a
// top-score table (rank 0 = best, rank 2 = worst - the display order). The ROM stores the score as
// three packed-decimal bytes (low pair first, ceiling 999999) and the name as six charmap glyphs
// ($00-delimited when the name is short); the wire codec owns both conversions. The name holds raw
// CharTile glyphs: the name-entry wheel's vocabulary (LETTER_A .. "z", "×", HEART, SPACE) is fully
// covered by the CharTile enum. A short name's trailing $00 aliases the digit-0 glyph but is unreachable
// from the wheel, so no collision exists in practice (contract-recorded).
struct TopScoreEntry {
    std::uint32_t score = 0;              // decimal; wire = 3 BCD bytes, ceiling 999999
    std::array<CharTile, 6> name{};       // charmap glyphs, first glyph lowest wire address

    friend bool operator==(const TopScoreEntry&, const TopScoreEntry&) = default;
};

struct HighScoreState {
    // $D000 wTypeBTopScores: 10 levels x 6 starting heights x 3 ranks (1620 wire bytes). Indexed
    // [level][height][rank]; rank 0 is the best score for that (level, height).
    std::array<std::array<std::array<TopScoreEntry, 3>, 6>, 10> typeB{};

    // $D654 wTypeATopScores: 10 levels x 3 ranks (270 wire bytes). Indexed [level][rank].
    std::array<std::array<TopScoreEntry, 3>, 10> typeA{};

    // Type C's own scores, in Type A's shape: 10 levels x 3 ranks. The port's own mode, so this table
    // answers to no work-RAM address - the cartridge has nowhere to put it. It persists in the same
    // document as the other two, appended after them.
    std::array<std::array<TopScoreEntry, 3>, 10> typeC{};

    // $FFC7 hNewTopScore: set when the just-finished game earned a top score (routes the menu into
    // name entry), cleared once name entry is submitted or the game starts. Domain {0,1}.
    bool newTopScore = false;

    // $FFE8 hRedrawTopScoresDuringVBlank: the staged top-score rows need flushing to VRAM next
    // VBlank; the VBlank routine consumes and clears it. Domain {0,1}; named after EngineState's
    // scoreRedrawRequested.
    bool topScoresRedrawRequested = false;

    // $FFC8 (unlabelled): the rank the new score took, as the ROM's inverted counter - 3 = 1st place,
    // 2 = 2nd, 1 = 3rd. The wire value is kept verbatim (the inversion is the original's; see the
    // contract), so this is not a 0-based index.
    std::uint8_t newScoreRank = 0;

    // $FFC6 (unlabelled, shared byte): the column 0..5 the name-entry cursor sits on. This byte is
    // physically shared with GameFlowState::coarseCountdown ($FFC6); the two uses are disjoint in
    // time - the same overlay split as topOutLockCount / tempPreviewPiece. See the contract.
    std::uint8_t nameEntryColumn = 0;

    // Return every field to its boot (all-zero) value.
    void reset() { *this = HighScoreState{}; }

    friend bool operator==(const HighScoreState&, const HighScoreState&) = default;
};

}  // namespace kirpich
