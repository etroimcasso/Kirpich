#include "systems/stats_pages.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include <kirpich/char_tile.h>
#include <kirpich/music_type.h>

#include "data/sfx.h"              // SquareSfxId
#include "systems/readouts.h"      // drawNumber
#include "systems/rising_floor.h"  // kTypeCRiseValues
#include "systems/screen.h"        // writeMapText

namespace kirpich::systems {

namespace {

// ── What the pages are called ─────────────────────────────────────────────────────────────────────
//
// Written without commas or apostrophes, and in one case, because that is what the font has.

constexpr std::string_view kAllTimeTitles[] = {
    "play time", "rounds", "score", "clears", "pieces", "favourites",
};
constexpr std::string_view kModeTitles[]         = {"figures", "pieces"};
constexpr std::string_view kAchievementsTitles[] = {"achievements"};

// Which page of a branch draws the seven shapes.
constexpr std::size_t kAllTimePiecesPage = 4;
constexpr std::size_t kModePiecesPage    = 1;

// What a figure with nothing behind it says. A fold over an empty table has no answer, and a first
// slot shown as though it did would read as a real one.
constexpr std::string_view kNothingYet = "none";

// What the branch says until achievements are built. The row exists and leads somewhere; what it
// leads to is honest about being unfinished.
constexpr std::string_view kNotBuiltYet = "not built yet";

constexpr auto kCursorGlyph = static_cast<std::uint8_t>(CharTile::HYPHEN);

// ── Short values, carried by value ────────────────────────────────────────────────────────────────
//
// A page's values are worked out as it is drawn and written straight into the map, so nothing has to
// outlive the call that draws it - which is the property the paged screen is built around.

struct ShortText {
    std::array<char, 16> chars{};
    std::uint8_t         size = 0;

    [[nodiscard]] std::string_view view() const { return {chars.data(), size}; }
};

ShortText numberText(std::uint32_t value) {
    ShortText text;
    const int written = std::snprintf(text.chars.data(), text.chars.size(), "%u",
                                      static_cast<unsigned>(value));
    text.size         = written > 0 ? static_cast<std::uint8_t>(written) : 0;
    return text;
}

// The letter a game type is named by on screen. Lowercase, as every other string these screens draw
// is: the font has one case.
char typeLetter(GameType type) {
    switch (type) {
        case GameType::TYPE_B: return 'b';
        case GameType::TYPE_C: return 'c';
        case GameType::TYPE_A: break;
    }
    return 'a';
}

// A Type C rise as the interval the player picked rather than as the index it is stored at. The
// stored index is 0-5 and appears nowhere in the game, so printing it would show a number nobody has
// ever seen.
unsigned riseValueAt(std::uint8_t index) {
    const std::size_t slot = std::min<std::size_t>(index, kTypeCRiseChoiceCount - 1);
    return kTypeCRiseValues[slot];
}

// Which combination a round was played at: "a-5" for Type A, which is picked by level alone, and
// "b-1-3" or "c-7-12" for the two picked with a second value.
//
// The separator is a hyphen because the font has no slash. It also has no colon and no question mark;
// what it does have inside the range both tile sets share is letters, digits, a period and a hyphen,
// and text a build cannot spell writes nothing at all (writeMapText, systems/screen.h).
ShortText combinationText(const RoundCombination& at) {
    ShortText  text;
    const char type = typeLetter(at.type);

    int written = 0;
    if (!at.hasVariant) {
        written = std::snprintf(text.chars.data(), text.chars.size(), "%c-%u", type,
                                static_cast<unsigned>(at.level));
    } else {
        const unsigned second =
            at.type == GameType::TYPE_C ? riseValueAt(at.variant) : static_cast<unsigned>(at.variant);
        written = std::snprintf(text.chars.data(), text.chars.size(), "%c-%u-%u", type,
                                static_cast<unsigned>(at.level), second);
    }

    text.size = written > 0 ? static_cast<std::uint8_t>(written) : 0;
    return text;
}

// What a music selection is called on the selection screen it was made on.
std::string_view musicName(MusicType type) {
    switch (type) {
        case MusicType::MUSIC_A: return "a";
        case MusicType::MUSIC_B: return "b";
        case MusicType::MUSIC_C: return "c";
        case MusicType::OFF:     break;
    }
    return "off";
}

// ── The picker ────────────────────────────────────────────────────────────────────────────────────
//
// Each row is one axis, and a row's positions are `all` followed by that axis's own values - so
// position 0 folds the axis and position n + 1 is value n.

std::size_t axisCount(std::size_t row) noexcept {
    return row == 0 ? kStatLevels : kStatVariants;
}

std::uint8_t axisValue(const ScreenUiState& ui, std::size_t row) noexcept {
    return row == 0 ? ui.statsLevel : ui.statsVariant;
}

void setAxisValue(ScreenUiState& ui, std::size_t row, std::uint8_t value) noexcept {
    (row == 0 ? ui.statsLevel : ui.statsVariant) = value;
}

std::size_t axisPosition(std::uint8_t value, std::size_t count) noexcept {
    return value >= count ? 0 : static_cast<std::size_t>(value) + 1;
}

std::uint8_t axisValueAt(std::size_t position) noexcept {
    return position == 0 ? kStatAxisAll : static_cast<std::uint8_t>(position - 1);
}

std::string_view pickerLabel(StatsBranch branch, std::size_t row) noexcept {
    if (row == 0) return "level";
    return branch == StatsBranch::MODE_C ? "rise" : "height";
}

ShortText pickerValueText(StatsBranch branch, const ScreenUiState& ui, std::size_t row) {
    const std::uint8_t value = axisValue(ui, row);
    if (value >= axisCount(row)) {
        ShortText text;
        text.chars = {'a', 'l', 'l'};
        text.size  = 3;
        return text;
    }
    if (row == 1 && branch == StatsBranch::MODE_C) {
        return numberText(riseValueAt(value));
    }
    return numberText(value);
}

// The picker's rows, in the settings screen's own scroller columns: the label, the value, an arrow
// at each end there is somewhere to go, and the cursor beside the row the player is on.
void paintPicker(GameContext& game, StatsBranch branch) {
    const ScreenUiState& ui   = game.screens;
    BackgroundMap&       map  = game.display.displayedMap();
    const std::size_t    rows = statsPickerRowCount(branch);

    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t line = kStatsPickerFirstRow + kStatsPickerStride * row;

        writeMapText(map, line, kStatsLabelCol, pickerLabel(branch, row));
        writeMapText(map, line, kOptionValueCol, pickerValueText(branch, ui, row).view());

        const std::size_t count    = axisCount(row);
        const std::size_t position = axisPosition(axisValue(ui, row), count);
        placeScrollerArrows(game, row * 2, line, position > 0, position + 1 <= count);

        if (row == ui.statsPickerRow && ui.cursorVisible) {
            map[line][kStatsCursorCol] = kCursorGlyph;
        }
    }
}

// ── The pages themselves ──────────────────────────────────────────────────────────────────────────

void paintPieceCounts(BackgroundMap& map, const std::array<std::uint32_t, kPieceKindCount>& counts,
                      bool underPicker) {
    for (std::size_t kind = 0; kind < kPieceKindCount; ++kind) {
        const StatsPieceSlot slot = statsPieceSlot(kind);
        drawNumber(map, statsPieceCountLine(slot.row, underPicker), kStatsPieceCols[slot.column],
                   counts[kind], kStatsPieceCountPairs);
    }
}

void paintAllTimePage(BackgroundMap& map, const StatsState& stats, std::size_t page) {
    const StatSlice life = lifetimeTotals(stats);

    switch (page) {
        case 0: {
            const LongestRound best = longestRound(stats);
            statTextLine(map, kStatsFirstLine + 0, "program",
                         formatDuration(stats.applicationSeconds).view());
            statTextLine(map, kStatsFirstLine + 1, "rounds", formatDuration(life.seconds).view());
            statTextLine(map, kStatsFirstLine + 2, "longest",
                         formatDuration(best.seconds).view());
            statTextLine(map, kStatsFirstLine + 3, "at",
                         best.any ? combinationText(best.at).view() : kNothingYet);
            return;
        }
        case 1:
            statLine(map, kStatsFirstLine + 0, "rounds", life.rounds, 3);
            statLine(map, kStatsFirstLine + 1, "mode a", roundsFor(stats, GameType::TYPE_A), 3);
            statLine(map, kStatsFirstLine + 2, "mode b", roundsFor(stats, GameType::TYPE_B), 3);
            statLine(map, kStatsFirstLine + 3, "mode c", roundsFor(stats, GameType::TYPE_C), 3);
            return;
        case 2:
            statLine(map, kStatsFirstLine + 0, "score", life.score, 5);
            statLine(map, kStatsFirstLine + 1, "lines", life.lines, 3);
            statLine(map, kStatsFirstLine + 2, "drops", life.drops, 3);
            return;
        case 3:
            statLine(map, kStatsFirstLine + 0, "singles", life.singles, 3);
            statLine(map, kStatsFirstLine + 1, "doubles", life.doubles, 3);
            statLine(map, kStatsFirstLine + 2, "triples", life.triples, 3);
            statLine(map, kStatsFirstLine + 3, "tetrises", life.tetrises, 3);
            return;
        case kAllTimePiecesPage:
            paintPieceCounts(map, life.pieces, /*underPicker=*/false);
            return;
        default: break;
    }

    const FavouriteMode  mode  = favouriteMode(stats);
    const FavouriteMusic music = favouriteMusic(stats);
    const PreferredLevel level = preferredLevel(stats);

    const char letter = typeLetter(mode.type);
    statTextLine(map, kStatsFirstLine + 0, "mode",
                 mode.any ? std::string_view{&letter, 1} : kNothingYet);
    statTextLine(map, kStatsFirstLine + 1, "music",
                 music.any ? musicName(music.type) : kNothingYet);
    statTextLine(map, kStatsFirstLine + 2, "level",
                 level.any ? numberText(level.level).view() : kNothingYet);
}

void paintModePage(GameContext& game, StatsBranch branch, std::size_t page) {
    paintPicker(game, branch);

    BackgroundMap&  map    = game.display.displayedMap();
    const StatSlice totals = totalsForSelection(game.stats, statsSelection(game.screens, branch));

    if (page == kModePiecesPage) {
        paintPieceCounts(map, totals.pieces, /*underPicker=*/true);
        return;
    }

    statLine(map, kStatsModeFirstLine + 0, "rounds", totals.rounds, 3);
    statTextLine(map, kStatsModeFirstLine + 1, "played", formatDuration(totals.seconds).view());
    statTextLine(map, kStatsModeFirstLine + 2, "longest",
                 formatDuration(totals.longestRoundSeconds).view());
    statLine(map, kStatsModeFirstLine + 3, "score", totals.score, 5);
    statLine(map, kStatsModeFirstLine + 4, "lines", totals.lines, 3);
    statLine(map, kStatsModeFirstLine + 5, "drops", totals.drops, 3);
}

void paintStatsPage(GameContext& game, std::size_t page) {
    const StatsBranch branch = statsBranchOf(game.screens.statsBranch);

    switch (branch) {
        case StatsBranch::ALL_TIME:
            paintAllTimePage(game.display.displayedMap(), game.stats, page);
            return;
        case StatsBranch::ACHIEVEMENTS:
            writeMapText(game.display.displayedMap(), kStatsFirstLine, kStatsLabelCol, kNotBuiltYet);
            return;
        case StatsBranch::MODE_A:
        case StatsBranch::MODE_B:
        case StatsBranch::MODE_C:
            break;
    }
    paintModePage(game, branch, page);
}

// ── Walking and changing ──────────────────────────────────────────────────────────────────────────

// A game type's pages own their whole vertical walk: down the picker's rows and then on to the next
// page, which is what makes the two one motion rather than two modes. The all-time and achievements
// branches have no rows, so their steps fall through to the screen's own page turn.
bool walkStatsPage(GameContext& game, std::size_t page, int delta) {
    const StatsBranch branch = statsBranchOf(game.screens.statsBranch);
    if (!statsBranchIsMode(branch)) {
        return false;
    }

    ScreenUiState&    ui    = game.screens;
    const std::size_t rows  = statsPickerRowCount(branch);
    const std::size_t pages = statsPageCount(branch);

    if (delta > 0) {
        if (static_cast<std::size_t>(ui.statsPickerRow) + 1 < rows) {
            ++ui.statsPickerRow;
        } else if (page + 1 < pages) {
            ui.statsPage       = static_cast<std::uint8_t>(page + 1);
            ui.statsPickerRow  = 0;
        } else {
            return true;  // the last row of the last page: an end stop, and not a page turn either
        }
        game.audioCues.square = SquareSfxId::TINK;
        return true;
    }

    if (ui.statsPickerRow > 0) {
        --ui.statsPickerRow;
    } else if (page > 0) {
        ui.statsPage      = static_cast<std::uint8_t>(page - 1);
        ui.statsPickerRow = rows > 0 ? static_cast<std::uint8_t>(rows - 1) : 0;
    } else {
        // The top row of the first page is an end stop, not a way out. B is what leaves a screen,
        // everywhere in the game.
        return true;
    }
    game.audioCues.square = SquareSfxId::TINK;
    return true;
}

// Left and right move the value on the row the cursor is on, with the end stops every scroller in the
// game has.
void adjustStatsPicker(GameContext& game, std::size_t /*page*/, int delta) {
    const StatsBranch branch = statsBranchOf(game.screens.statsBranch);
    if (!statsBranchIsMode(branch)) {
        return;
    }

    ScreenUiState&    ui  = game.screens;
    const std::size_t row = std::min<std::size_t>(ui.statsPickerRow, statsPickerRowCount(branch) - 1);

    const std::size_t count    = axisCount(row);
    const int         position = static_cast<int>(axisPosition(axisValue(ui, row), count)) + delta;
    if (position < 0 || position > static_cast<int>(count)) {
        return;
    }

    setAxisValue(ui, row, axisValueAt(static_cast<std::size_t>(position)));
    game.audioCues.square = SquareSfxId::TINK;
}

}  // namespace

StatsBranch statsBranchOf(std::uint8_t row) noexcept {
    return row < kStatsBranchCount ? static_cast<StatsBranch>(row) : StatsBranch::ALL_TIME;
}

std::size_t statsPageCount(StatsBranch branch) noexcept {
    switch (branch) {
        case StatsBranch::ALL_TIME:     return std::size(kAllTimeTitles);
        case StatsBranch::ACHIEVEMENTS: return std::size(kAchievementsTitles);
        case StatsBranch::MODE_A:
        case StatsBranch::MODE_B:
        case StatsBranch::MODE_C:       break;
    }
    return std::size(kModeTitles);
}

std::string_view statsPageTitle(StatsBranch branch, std::size_t page) noexcept {
    const auto name = [page](const auto& titles) {
        return titles[std::min(page, std::size(titles) - 1)];
    };
    switch (branch) {
        case StatsBranch::ALL_TIME:     return name(kAllTimeTitles);
        case StatsBranch::ACHIEVEMENTS: return name(kAchievementsTitles);
        case StatsBranch::MODE_A:
        case StatsBranch::MODE_B:
        case StatsBranch::MODE_C:       break;
    }
    return name(kModeTitles);
}

bool statsBranchIsMode(StatsBranch branch) noexcept {
    return branch == StatsBranch::MODE_A || branch == StatsBranch::MODE_B ||
           branch == StatsBranch::MODE_C;
}

GameType statsBranchType(StatsBranch branch) noexcept {
    switch (branch) {
        case StatsBranch::MODE_B: return GameType::TYPE_B;
        case StatsBranch::MODE_C: return GameType::TYPE_C;
        case StatsBranch::ALL_TIME:
        case StatsBranch::MODE_A:
        case StatsBranch::ACHIEVEMENTS: break;
    }
    return GameType::TYPE_A;
}

bool statsPageIsPieces(StatsBranch branch, std::size_t page) noexcept {
    if (branch == StatsBranch::ALL_TIME) return page == kAllTimePiecesPage;
    return statsBranchIsMode(branch) && page == kModePiecesPage;
}

std::size_t statsPickerRowCount(StatsBranch branch) noexcept {
    switch (branch) {
        case StatsBranch::MODE_A: return 1;  // Type A is picked by level alone
        case StatsBranch::MODE_B:
        case StatsBranch::MODE_C: return 2;
        case StatsBranch::ALL_TIME:
        case StatsBranch::ACHIEVEMENTS: break;
    }
    return 0;
}

StatSelection statsSelection(const ScreenUiState& ui, StatsBranch branch) noexcept {
    return StatSelection{.type    = statsBranchType(branch),
                         .level   = ui.statsLevel,
                         .variant = ui.statsVariant};
}

std::array<std::uint32_t, kPieceKindCount> statsPieceCounts(const StatsState&    stats,
                                                            const ScreenUiState& ui,
                                                            StatsBranch          branch) {
    if (statsBranchIsMode(branch)) {
        return totalsForSelection(stats, statsSelection(ui, branch)).pieces;
    }
    return lifetimeTotals(stats).pieces;
}

void statLine(BackgroundMap& map, std::size_t line, std::string_view label, std::uint32_t value,
              std::uint8_t digitPairs) {
    writeMapText(map, line, kStatsLabelCol, label);

    const std::size_t width = std::size_t{2} * digitPairs;
    const std::size_t col   = width > kStatsValueEndCol ? 0 : kStatsValueEndCol + 1 - width;
    drawNumber(map, line, col, value, digitPairs);
}

void statTextLine(BackgroundMap& map, std::size_t line, std::string_view label,
                  std::string_view value) {
    writeMapText(map, line, kStatsLabelCol, label);

    const std::size_t col = value.size() > kStatsValueEndCol + 1 - kStatsLabelCol
                                ? kStatsLabelCol
                                : kStatsValueEndCol + 1 - value.size();
    writeMapText(map, line, col, value);
}

PageWiring statsPageWiring() {
    return PageWiring{
        .count = [](const GameContext& game) {
            return statsPageCount(statsBranchOf(game.screens.statsBranch));
        },
        .title =
            [](const GameContext& game, std::size_t page) {
                return statsPageTitle(statsBranchOf(game.screens.statsBranch), page);
            },
        .paintPage = [](GameContext& game, std::size_t page) { paintStatsPage(game, page); },
        // B leaves the branch for the screen that opened it, which is what an unset seam does.
        .walk = [](GameContext& game, std::size_t page,
                   int delta) { return walkStatsPage(game, page, delta); },
        .adjust = [](GameContext& game, std::size_t page,
                     int delta) { adjustStatsPicker(game, page, delta); },
    };
}

}  // namespace kirpich::systems
