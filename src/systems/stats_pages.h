#pragma once

// What the statistics pages hold: which pages each branch has, what each one is called, and what goes
// on it.
//
// The paged screen (systems/page_screen.h) is the machine and owns no words; this is the content,
// beside systems/enhancement_screens.h and systems/stats_screens.h, which do the same for the screens
// a settings row opens. One page-screen instance serves all five branches: which branch is up is on
// ScreenUiState, and everything below forks on it.
//
// A page is a group of related figures, and the heading names the group. Nothing here is stored: every
// figure is a fold over the per-combination slices at the moment it is drawn (systems/stats.h), so
// there is no second copy of any total to fall out of step with the table.
//
// A game type's two pages share one picker, which is what makes the figures under it change as it
// moves. Its rows are scrollers in the settings screen's own columns, so they line up with every other
// scroller the player walked through to get here; the walk down them and the turn to the next page are
// one continuous motion, as the settings screen's own walk across its pages is. Both axes open on
// kStatAxisAll - the mode's own aggregate - so no page has to exist for the total.
//
// Every value names what the player picked rather than how it is stored. A Type C rise reads as its
// interval - 16, 14, 12, 10, 8, 6 - and never as the 0-5 index behind it, which is a number that
// appears nowhere else in the game.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/piece_kind.h>

#include "state/display_state.h"  // BackgroundMap
#include "state/screen_ui_state.h"
#include "state/stats_state.h"
#include "systems/page_screen.h"      // PageWiring, GameContext
#include "systems/settings_screen.h"  // kPageDownArrowRow, the scroller columns
#include "systems/stats.h"            // StatSelection

namespace kirpich::systems {

// Which of the statistics screen's five rows opened the branch on display. The values are the rows'
// own order, because that is what the chooser reports and what ScreenUiState::statsBranch carries.
enum class StatsBranch : std::uint8_t {
    ALL_TIME     = 0,
    MODE_A       = 1,
    MODE_B       = 2,
    MODE_C       = 3,
    ACHIEVEMENTS = 4,
};

// How many branches there are. Tied to the last enumerator so the chooser's rows and this cannot
// drift.
inline constexpr std::size_t kStatsBranchCount =
    static_cast<std::size_t>(StatsBranch::ACHIEVEMENTS) + 1;

// The branch a stored row names. A row outside the five reads as the first, so a stale value shows a
// real page rather than reaching outside the tables.
[[nodiscard]] StatsBranch statsBranchOf(std::uint8_t row) noexcept;

// ── Where the drawn parts sit ─────────────────────────────────────────────────────────────────────
//
// Cells, not pixels: (row, column) in the background map, the same units the rest of the screen uses.

// A figure's label starts here and its value ends here, so the numbers line up down the page however
// wide each one is. The cursor column is the list's, so a picker row's cursor sits where a list row's
// does.
//
// The value stops two cells short of the twentieth, which is the last one the display shows. A figure
// that ran to the edge read as though it had been cut off there, and the page had a margin down one
// side and none down the other.
inline constexpr std::size_t kStatsCursorCol   = 1;
inline constexpr std::size_t kStatsLabelCol    = 3;
inline constexpr std::size_t kStatsValueEndCol = 17;

// Where a page with no picker starts its lines, one to a row.
inline constexpr std::size_t kStatsFirstLine = 5;

// The picker's rows, one under the other rather than at the settings screen's three-row pitch.
//
// The two rows are one control and read as a pair, and the rows they give up are what the pieces page
// needs: a shape is two cells tall, so three rows of shape-over-count take nine rows, the down
// arrow's row has to stay clear, and the grid has to start a clear row below the picker. Those four
// do not all fit with the rows any further apart.
inline constexpr std::size_t kStatsPickerFirstRow = 4;
inline constexpr std::size_t kStatsPickerStride   = 1;
inline constexpr std::size_t kStatsPickerMaxRows  = 2;

// Where a game type's figures start. One row, whether the picker above has one row or two, so both
// modes' pages read the same way.
inline constexpr std::size_t kStatsModeFirstLine = 9;

// The pieces grid: three columns across and three rows down, a shape with its count on the row
// underneath it. Wide and short rather than tall and narrow, because a shape at its spawn orientation
// is two cells tall and a taller grid leaves no room between one shape and the next - which reads as
// one shape rather than seven.
//
// A block is three rows: two for the shape and one for its count, which is also the row that
// separates it from the block below.
inline constexpr std::size_t kStatsPieceGridCols   = 3;
inline constexpr std::size_t kStatsPieceGridRows   = 3;
inline constexpr std::size_t kStatsPieceBlockRows  = 3;

// The grid's first row, with a picker above it and without one.
inline constexpr std::size_t kStatsPieceFirstRowAlone       = 5;
inline constexpr std::size_t kStatsPieceFirstRowUnderPicker = 7;

// Where a column starts. A column is six cells: the count fills them, and the shape takes the first
// four of them - four because the line piece is four wide at its spawn orientation. That leaves two
// clear cells between one shape and the next, and the first and last cells of the row clear.
inline constexpr std::array<std::size_t, kStatsPieceGridCols> kStatsPieceCols{1, 7, 13};
inline constexpr std::uint8_t kStatsPieceCountPairs = 3;

// Where one shape sits in the grid: which column, and which row of it.
struct StatsPieceSlot {
    std::size_t column = 0;
    std::size_t row    = 0;

    friend constexpr bool operator==(const StatsPieceSlot&, const StatsPieceSlot&) = default;
};

// The slot a shape is drawn in, by PieceKind index, filling the grid a row at a time.
[[nodiscard]] constexpr StatsPieceSlot statsPieceSlot(std::size_t kind) noexcept {
    return StatsPieceSlot{.column = kind % kStatsPieceGridCols,
                          .row    = kind / kStatsPieceGridCols};
}

// The map row a grid row's shape starts on, and the row its count is drawn on, with a picker above
// the grid and without one.
[[nodiscard]] constexpr std::size_t statsPieceLine(std::size_t row, bool underPicker) noexcept {
    const std::size_t first =
        underPicker ? kStatsPieceFirstRowUnderPicker : kStatsPieceFirstRowAlone;
    return first + kStatsPieceBlockRows * row;
}

[[nodiscard]] constexpr std::size_t statsPieceCountLine(std::size_t row,
                                                        bool        underPicker) noexcept {
    return statsPieceLine(row, underPicker) + 2;
}

// The lowest row the grid writes has to clear the down arrow's row.
static_assert(statsPieceCountLine(kStatsPieceGridRows - 1, /*underPicker=*/true) <
                  kPageDownArrowRow,
              "the pieces grid must stop short of the down arrow's row");

// And it has to start a clear row below the picker's last row, or the first shape sits against the
// row naming the axis above it.
static_assert(kStatsPieceFirstRowUnderPicker >
                  kStatsPickerFirstRow + kStatsPickerStride * (kStatsPickerMaxRows - 1) + 1,
              "the pieces grid must leave a clear row between the picker and the first shape");

// ── The pages ─────────────────────────────────────────────────────────────────────────────────────

// How many pages a branch offers, and what the one at `page` is called. A page outside the branch's
// own count is named by its last page, which is what the screen would be showing anyway.
[[nodiscard]] std::size_t      statsPageCount(StatsBranch branch) noexcept;
[[nodiscard]] std::string_view statsPageTitle(StatsBranch branch, std::size_t page) noexcept;

// Whether a branch reads one game type, and which one. The all-time and achievements branches read
// none, and statsBranchType then reports Type A rather than an invalid value; callers test the first.
[[nodiscard]] bool     statsBranchIsMode(StatsBranch branch) noexcept;
[[nodiscard]] GameType statsBranchType(StatsBranch branch) noexcept;

// Whether the page at `page` is the one that draws the seven shapes. The render bridge asks this to
// know whether to place their sprites.
[[nodiscard]] bool statsPageIsPieces(StatsBranch branch, std::size_t page) noexcept;

// ── The picker ────────────────────────────────────────────────────────────────────────────────────

// How many picker rows a branch's pages carry: none outside a game type, one for Type A, which is
// picked by level alone, and two for the other two.
[[nodiscard]] std::size_t statsPickerRowCount(StatsBranch branch) noexcept;

// What the pages are currently reading. Both axes are taken as they stand, and an axis on
// kStatAxisAll folds that axis away (systems/stats.h).
[[nodiscard]] StatSelection statsSelection(const ScreenUiState& ui, StatsBranch branch) noexcept;

// The seven per-shape counts the branch's pieces page shows: the whole game's on the all-time branch,
// and the picker's own selection on a game type's.
[[nodiscard]] std::array<std::uint32_t, kPieceKindCount> statsPieceCounts(const StatsState& stats,
                                                                          const ScreenUiState& ui,
                                                                          StatsBranch branch);

// ── Drawing a line ────────────────────────────────────────────────────────────────────────────────

// One labelled figure: the label from kStatsLabelCol, the value ending at kStatsValueEndCol.
//
// The width is per figure because the two ends share twenty cells - a lifetime score wants five digit
// pairs and leaves seven cells for its label, where a clear count is happy with three and leaves
// eleven. Digits above the width are dropped rather than widening the field, which is the printer's
// own law (drawNumber, systems/readouts.h), so a figure's width has to be chosen for what it draws.
void statLine(BackgroundMap& map, std::size_t line, std::string_view label, std::uint32_t value,
              std::uint8_t digitPairs);

// The same line with text instead of a number - a duration, a combination, or a name. A value wider
// than the space left starts at the label's own column rather than before it.
void statTextLine(BackgroundMap& map, std::size_t line, std::string_view label,
                  std::string_view value);

// ── Wiring ────────────────────────────────────────────────────────────────────────────────────────

// The page screen's wiring for the statistics: the five branches' page counts and headings, the
// paint, the picker's walk and its value changes.
[[nodiscard]] PageWiring statsPageWiring();

}  // namespace kirpich::systems
