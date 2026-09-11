#include "systems/achievements_screen.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <span>

#include <kirpich/action.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "state/achievements_screen_state.h"
#include "systems/game_state_dispatcher.h"
#include "systems/menu_screens.h"     // clearOamObjects
#include "systems/screen_stack.h"     // popScreen
#include "systems/settings_screen.h"  // blinkScreenCursor

namespace kirpich::systems {

namespace {

// The interval every port screen's cursor blinks on.
constexpr std::uint8_t kBlinkFrames = 16;

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

void moveCue(GameContext& game) { game.audioCues.square = SquareSfxId::TINK; }
void screenCue(GameContext& game) { game.audioCues.square = SquareSfxId::CHANGE_SCREEN; }

// The run of the set a section occupies. The set is grouped by section in id order, so a section is
// one contiguous run; the assert holds that, since a gap would make a count and a walk disagree.
struct SectionRange {
    std::size_t begin = 0;
    std::size_t count = 0;
};

SectionRange sectionRange(AchievementSection section) noexcept {
    const std::span<const AchievementDef> set = achievementSet();

    std::size_t begin = 0;
    std::size_t end   = 0;
    bool        found = false;
    for (std::size_t i = 0; i < set.size(); ++i) {
        if (set[i].section != section) continue;
        if (!found) {
            begin = i;
            found = true;
        }
        end = i + 1;
    }
    if (!found) return {};

    for (std::size_t i = begin; i < end; ++i) {
        assert(set[i].section == section &&
               "a section's badges must be one contiguous run of the set");
    }
    return {.begin = begin, .count = end - begin};
}

std::size_t cursorRow(std::uint8_t cursor) noexcept { return cursor / kAchievementGridCols; }
std::size_t cursorCol(std::uint8_t cursor) noexcept { return cursor % kAchievementGridCols; }

// Turn to the section above or below. The cursor keeps its column and lands in the row it would have
// stepped into: the first row of the section below, the last row of the section above. A vertical
// step therefore means the same thing at a section boundary as it does inside one, which is what
// makes the whole set read as a single grid rather than as nine of them. An end - the first section's
// top, the last section's bottom - turns nothing.
void turnSection(GameContext& game, int delta) {
    AchievementScreenState& ui   = game.achievementScreen;
    const int               next = static_cast<int>(ui.section) + delta;
    if (next < 0 || next >= static_cast<int>(kAchievementSectionCount)) return;

    const std::size_t column = cursorCol(ui.cursor);

    ui.section              = static_cast<std::uint8_t>(next);
    const std::size_t count = achievementSectionBadgeCount(achievementSectionAt(ui.section));
    if (count == 0) {
        ui.cursor = 0;
        moveCue(game);
        return;
    }

    // The arriving row can be shorter than the one left, so the column clamps to the last badge in
    // it rather than landing past the end of the section.
    const std::size_t row  = delta > 0 ? 0 : (count - 1) / kAchievementGridCols;
    const std::size_t slot = std::min(row * kAchievementGridCols + column, count - 1);

    ui.cursor = static_cast<std::uint8_t>(slot);
    moveCue(game);
}

// One step of the cursor. Left and right stop at the row's ends; up and down leave the section when
// there is no row that way, which is what makes the whole set one walk.
void moveCursor(GameContext& game, int dCol, int dRow) {
    AchievementScreenState&  ui      = game.achievementScreen;
    const AchievementSection section = achievementSectionAt(ui.section);
    const std::size_t        count   = achievementSectionBadgeCount(section);
    if (count == 0) return;

    if (dCol < 0) {
        if (cursorCol(ui.cursor) == 0) return;
        --ui.cursor;
        moveCue(game);
        return;
    }
    if (dCol > 0) {
        if (cursorCol(ui.cursor) + 1 >= kAchievementGridCols ||
            static_cast<std::size_t>(ui.cursor) + 1 >= count) {
            return;
        }
        ++ui.cursor;
        moveCue(game);
        return;
    }
    if (dRow < 0) {
        if (cursorRow(ui.cursor) == 0) {
            turnSection(game, -1);
            return;
        }
        ui.cursor = static_cast<std::uint8_t>(ui.cursor - kAchievementGridCols);
        moveCue(game);
        return;
    }
    if (cursorRow(ui.cursor) >= (count - 1) / kAchievementGridCols) {
        turnSection(game, +1);
        return;
    }
    ui.cursor = static_cast<std::uint8_t>(
        std::min<std::size_t>(ui.cursor + kAchievementGridCols, count - 1));
    moveCue(game);
}

void openBadge(GameContext& game) {
    AchievementScreenState&  ui      = game.achievementScreen;
    const AchievementSection section = achievementSectionAt(ui.section);
    if (achievementSectionBadgeCount(section) == 0) return;

    ui.openId = achievementSectionBadge(section, ui.cursor).id;
    ui.open   = true;
    screenCue(game);
}

// ── The input tables ──────────────────────────────────────────────────────────────────────────────
//
// What a button does, as data: one table per mode. The screen has two - walking the grid, and reading
// a badge - and which mode it is in picks the table this frame's press is looked up in.

using Effect = void (*)(GameContext&);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kGridBinds{
    Bind{Action::MenuLeft, [](GameContext& g) { moveCursor(g, -1, 0); }},
    Bind{Action::MenuRight, [](GameContext& g) { moveCursor(g, +1, 0); }},
    Bind{Action::MenuUp, [](GameContext& g) { moveCursor(g, 0, -1); }},
    Bind{Action::MenuDown, [](GameContext& g) { moveCursor(g, 0, +1); }},
    Bind{Action::Confirm, [](GameContext& g) { openBadge(g); }},
    Bind{Action::Back,
         [](GameContext& g) {
             popScreen(g);
             screenCue(g);
         }},
};

constexpr std::array kPanelBinds{
    Bind{Action::Back,
         [](GameContext& g) {
             g.achievementScreen.open = false;
             screenCue(g);
         }},
};

void dispatch(GameContext& game, std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game);
            return;
        }
    }
}

}  // namespace

AchievementSection achievementSectionAt(std::uint8_t index) noexcept {
    return static_cast<AchievementSection>(
        std::min<std::size_t>(index, kAchievementSectionCount - 1));
}

std::string_view achievementSectionTitle(AchievementSection section) noexcept {
    switch (section) {
        case AchievementSection::ASCENT:        return "the ascent";
        case AchievementSection::ENDURANCE:     return "endurance";
        case AchievementSection::THE_TETRIS:    return "the tetris";
        case AchievementSection::DIG_OUT:       return "dig out";
        case AchievementSection::RISING_FLOOR:  return "rising floor";
        case AchievementSection::REDLINE:       return "redline";
        case AchievementSection::HAVE_A_HEART:  return "have a heart";
        case AchievementSection::LIFTOFF:       return "liftoff";
        case AchievementSection::THE_LONG_GAME: return "the long game";
    }
    return "";
}

std::size_t achievementSectionBadgeCount(AchievementSection section) noexcept {
    return sectionRange(section).count;
}

const AchievementDef& achievementSectionBadge(AchievementSection section,
                                              std::size_t        position) noexcept {
    const SectionRange                    range = sectionRange(section);
    const std::span<const AchievementDef> set   = achievementSet();
    const std::size_t clamped = range.count == 0 ? 0 : std::min(position, range.count - 1);
    return set[range.begin + clamped];
}

bool achievementUnlocked(const AchievementState& earned, AchievementId id) noexcept {
    return earned.unlocked[achievementIndex(id)].unlocked;
}

void initAchievementsScreen(GameContext& game) {
    game.achievementScreen.reset();
    game.screens.cursorVisible = true;

    // The object buffer holds whatever the screen before this one put there. This screen's badges are
    // its own components, not buffer entries, so it starts the buffer empty.
    clearOamObjects(game);

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = GameState::ACHIEVEMENTS;
}

void achievementsScreen(GameContext& game) {
    blinkScreenCursor(game);
    dispatch(game, game.achievementScreen.open ? std::span<const Bind>{kPanelBinds}
                                               : std::span<const Bind>{kGridBinds});
}

void installAchievementsScreen(GameStateDispatcher& dispatcher) {
    dispatcher.setHandler(GameState::INIT_ACHIEVEMENTS,
                          [](GameContext& g) { initAchievementsScreen(g); });
    dispatcher.setHandler(GameState::ACHIEVEMENTS, [](GameContext& g) { achievementsScreen(g); });
}

}  // namespace kirpich::systems
