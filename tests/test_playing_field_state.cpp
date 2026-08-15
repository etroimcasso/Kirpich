// Playing-field state (the $C800 board + the $C400 attack staging row): the board struct
// (src/state/playing_field_state.h) checked against the existing layout/census and wipe fixtures. This
// unit ships no fixture of its own - the work-RAM census (tests/fixtures/wram_expected.h) already carries
// the one $C400 staging row and the 32 board rows the game reaches, and the playing-field wipe fixture
// (tests/fixtures/playing_field_expected.h) already carries the 18 shadow/VRAM address triples.
//
// The struct is a geometry over those addresses, not a byte image, so its fidelity is held by the
// fixtures: every censused byte in the two windows must resolve to one owner (a staging cell or a board
// cell), the struct's own row math must reproduce every wipe address (and the shadow/VRAM mirror offset),
// and the field accessor must map onto the grid. All sweeps are full-corpus over the fixtures - never a
// subset. Ownership expectations come from docs/contracts/playing-field-state.md.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <kirpich/char_tile.h>

#include "state/playing_field_state.h"
#include "data/playing_field.h"
#include "data/garbage.h"
#include "fixtures/wram_expected.h"
#include "fixtures/playing_field_expected.h"

namespace {

using kirpich::CharTile;
using kirpich::kAttackRowBrickTile;
using kirpich::kBoardCols;
using kirpich::kBoardRows;
using kirpich::kGarbageEmptyTile;
using kirpich::kPlayingFieldCols;
using kirpich::kPlayingFieldOriginCol;
using kirpich::kPlayingFieldRows;
using kirpich::kTypeBDemoGarbageRows;
using kirpich::PlayingFieldState;
using kirpich::playingFieldRowForWipeCounter;
using kirpich::fixtures::kExpectedPlayingFieldWipes;
using kirpich::fixtures::kWramCensus;

// The two windows this unit owns, and the boundaries of the neighbours it must not reach into.
constexpr std::uint16_t kStagingBase = 0xC400;
constexpr std::uint16_t kBoardBase = 0xC800;
constexpr std::uint16_t kBoardTop = 0xCC00;
constexpr std::uint16_t kUpperTop = 0xD000;

int wramCensusRefOf(std::uint16_t addr) {
    for (const auto& c : kWramCensus)
        if (c.address == addr) return c.refCount;
    return 0;
}

// The $C800-relative ROM address the field accessor maps a visible-field coordinate to: the board is
// row-major over the $20-byte stride, and the field's left edge is kPlayingFieldOriginCol.
constexpr std::uint16_t fieldCellRomAddr(std::size_t row, std::size_t col) {
    return static_cast<std::uint16_t>(kBoardBase + row * kBoardCols + kPlayingFieldOriginCol + col);
}

// (1) Census window resolution. Every censused byte in the two windows resolves to exactly one owner: the
// lone $C400 staging row, or a cell on the 32x32 board grid; nothing above the board except $CFFF.
TEST(PlayingFieldState, CensusWindowResolution) {
    // [$C400,$C800): exactly one row, $C400, refCount 3 (build / hole-poke / insert).
    int stagingRows = 0;
    for (const auto& c : kWramCensus) {
        if (c.address < kStagingBase || c.address >= kBoardBase) continue;
        ++stagingRows;
        EXPECT_EQ(c.address, kStagingBase) << std::hex << c.address;
        EXPECT_EQ(c.refCount, 3);
    }
    EXPECT_EQ(stagingRows, 1);

    // [$C800,$CC00): exactly 32 rows, each decomposing onto the grid.
    int boardRows = 0;
    for (const auto& c : kWramCensus) {
        if (c.address < kBoardBase || c.address >= kBoardTop) continue;
        ++boardRows;
        const std::size_t row = static_cast<std::size_t>(c.address - kBoardBase) >> 5;
        const std::size_t col = static_cast<std::size_t>(c.address - kBoardBase) & 0x1F;
        EXPECT_LT(row, kBoardRows) << std::hex << c.address;
        EXPECT_LT(col, kBoardCols) << std::hex << c.address;
    }
    EXPECT_EQ(boardRows, 32);

    // [$CC00,$D000): no board census row - the only address is the stack/boot marker $CFFF (audio-state).
    for (const auto& c : kWramCensus) {
        if (c.address < kBoardTop || c.address >= kUpperTop) continue;
        EXPECT_EQ(c.address, 0xCFFF) << std::hex << c.address;
    }

    // Corner refCount pins.
    EXPECT_EQ(wramCensusRefOf(0xC802), 6);
    EXPECT_EQ(wramCensusRefOf(0xC822), 3);
    EXPECT_EQ(wramCensusRefOf(0xC842), 2);
    EXPECT_EQ(wramCensusRefOf(0xC9A2), 3);
    EXPECT_EQ(wramCensusRefOf(0xCA22), 3);
    EXPECT_EQ(wramCensusRefOf(0xCA41), 1);
    EXPECT_EQ(wramCensusRefOf(0xCA42), 2);
    EXPECT_EQ(wramCensusRefOf(0xCBC2), 1);
}

// (2) Geometry vs the 1.F wipe fixture. For each of the 18 triples the shadow/VRAM mirror offset holds,
// and the struct's own row math reproduces the shadow address. Plus the board closed forms.
TEST(PlayingFieldState, GeometryMatchesWipeFixture) {
    EXPECT_EQ(kExpectedPlayingFieldWipes.size(), std::size_t{kPlayingFieldRows});
    for (const auto& w : kExpectedPlayingFieldWipes) {
        EXPECT_EQ(w.wram - w.vram, 0x3000) << "counter " << int(w.counter);
        const std::size_t fieldRow = playingFieldRowForWipeCounter(w.counter);
        EXPECT_EQ(static_cast<std::size_t>(w.wram - kBoardBase),
                  fieldRow * kBoardCols + kPlayingFieldOriginCol)
            << "counter " << int(w.counter);
    }

    PlayingFieldState s{};
    EXPECT_EQ(kBoardRows * kBoardCols, std::size_t{0x400});
    EXPECT_EQ(sizeof s.board, std::size_t{0x400});
    EXPECT_EQ(kBoardCols, std::size_t{0x20});
}

// (3) fieldCell mapping. Over the full visible-field domain the accessor lands on the board cell at the
// field origin, and the landmark cells map to their known ROM addresses.
TEST(PlayingFieldState, FieldCellMapping) {
    PlayingFieldState s{};
    for (std::size_t r = 0; r < kPlayingFieldRows; ++r)
        for (std::size_t c = 0; c < kPlayingFieldCols; ++c)
            EXPECT_EQ(&s.fieldCell(r, c), &s.board[r][kPlayingFieldOriginCol + c])
                << "r=" << r << " c=" << c;

    const PlayingFieldState& cs = s;              // const overload compiles and reads
    EXPECT_EQ(cs.fieldCell(5, 5), 0);

    // Landmark ROM addresses through the accessor's board mapping.
    EXPECT_EQ(fieldCellRomAddr(2, 0), 0xC842);    // line-clear scan start (the third-row quirk)
    EXPECT_EQ(fieldCellRomAddr(17, 0), 0xCA22);   // field bottom row
    EXPECT_EQ(fieldCellRomAddr(16, 0), 0xCA02);   // Type B procedural garbage fill base
    EXPECT_EQ(fieldCellRomAddr(14, 0), 0xC9C2);   // demo-garbage stamp
    EXPECT_EQ(kPlayingFieldRows - kTypeBDemoGarbageRows, 14);
    EXPECT_EQ(fieldCellRomAddr(13, 0), 0xC9A2);   // multiplayer received-garbage buffer
    // each landmark is at the field origin column (2) on the grid
    EXPECT_EQ(fieldCellRomAddr(17, 0), kBoardBase + 17 * kBoardCols + kPlayingFieldOriginCol);
}

// (4) Struct-shape pins. The staging row is one field width, the board is the 32x32 grid, both default
// to all-zero, and the defaulted == distinguishes a mutation.
TEST(PlayingFieldState, StructShapePins) {
    PlayingFieldState s{};
    EXPECT_EQ(s.attackRow.size(), std::size_t{kPlayingFieldCols});
    EXPECT_EQ(s.attackRow.size(), std::size_t{10});
    EXPECT_EQ(s.board.size(), kBoardRows);
    EXPECT_EQ(s.board[0].size(), kBoardCols);
    EXPECT_EQ(sizeof s.board, std::size_t{0x400});

    EXPECT_EQ(s, PlayingFieldState{});
    for (const auto& row : s.board)
        for (auto cell : row) EXPECT_EQ(cell, 0);
    for (auto cell : s.attackRow) EXPECT_EQ(cell, 0);

    PlayingFieldState other{};
    other.board[0][0] = 1;
    EXPECT_NE(other, s);   // the defaulted == distinguishes them
}

// (5) Reset restores boot. Mutate corners across the whole grid and the staging row, reset, compare fresh.
TEST(PlayingFieldState, ResetRestoresBoot) {
    PlayingFieldState s{};
    s.board[0][0]   = 0x8E;
    s.board[2][2]   = 0x80;
    s.board[17][11] = 0x81;
    s.board[18][2]  = 0x8E;
    s.board[30][2]  = 0x2F;
    s.board[31][31] = 0x99;
    s.attackRow.fill(kAttackRowBrickTile);

    EXPECT_FALSE(s == PlayingFieldState{});   // the mutations took
    s.reset();
    EXPECT_TRUE(s == PlayingFieldState{});     // back to boot state
}

// (6) Wire-value pins. The one hand-entered brick tile, the empty-cell tie to the character map, and the
// field origin column (which the wipe fixture's top-row source pins independently).
TEST(PlayingFieldState, WireValuePins) {
    EXPECT_EQ(kAttackRowBrickTile, 0x28);
    EXPECT_EQ(kGarbageEmptyTile, static_cast<std::uint8_t>(CharTile::SPACE));
    EXPECT_EQ(kGarbageEmptyTile, 0x2F);
    EXPECT_EQ(kPlayingFieldOriginCol, std::size_t{2});

    // The last wipe triple (counter 19) copies the top field row from $C802 - the field origin column.
    EXPECT_EQ(kExpectedPlayingFieldWipes.back().counter, 19);
    EXPECT_EQ(static_cast<std::size_t>(kExpectedPlayingFieldWipes.back().wram - kBoardBase),
              kPlayingFieldOriginCol);
}

}  // namespace
