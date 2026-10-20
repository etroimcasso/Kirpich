// The Palette settings screen — behavioral tests over its logic (src/systems/palette_settings_screen.h)
// and its components (src/render/screens/palette_settings/, src/render/components/piece_shape.h).
//
// Device-free. The logic is pure state over the game aggregate and the wirings' seams, and the
// components are pure functions returning primitives, so the atlas here is a plain value with
// distinguishable handles rather than anything uploaded. The screen is the port's own, so every
// asserted value comes from its stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/piece_kind.h>

#include "render/components/glyphs.h"
#include "render/components/piece_shape.h"
#include "render/palettes.h"
#include "render/screens/palette_settings/layout.h"
#include "render/screens/palette_settings/screen.h"
#include "retropp/input.h"
#include "state/custom_palettes.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/palette_settings_screen.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::CustomPalettes;
using kirpich::CustomPaletteSlot;
using kirpich::GameState;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::render::kCustomRamp;
using kirpich::render::kShadeRampCount;
using kirpich::render::ShadeRamp;
using kirpich::render::TileAtlas;
using kirpich::systems::CustomPaletteWiring;
using kirpich::systems::GameContext;
using kirpich::systems::SettingsWiring;

constexpr std::uint8_t kPlayerRamp = 7;
constexpr std::uint8_t kLastRamp   = static_cast<std::uint8_t>(kShadeRampCount - 1);

// The interval the game's own selection screens blink their cursors on (tetris.asm:3597-3608).
constexpr std::uint8_t kBlinkFrames = 16;

// The first settings page's own cells: the value on column 15, the arrows on 13 and 19, the first row
// on line 5, the cursor on column 1.
constexpr int kValueX      = 15 * 8;
constexpr int kLeftArrowX  = 13 * 8;
constexpr int kRightArrowX = 19 * 8;
constexpr int kCursorX     = 1 * 8;
constexpr int kLineY       = 5 * 8;

constexpr int kScreenWidth  = 160;
constexpr int kScreenHeight = 144;

TileAtlas makeAtlas() {
    TileAtlas atlas{
        .font             = static_cast<retropp::AtlasId>(11),
        .copyrightTitle   = static_cast<retropp::AtlasId>(22),
        .gameplay         = static_cast<retropp::AtlasId>(33),
        .multiplayerBuran = static_cast<retropp::AtlasId>(44),
    };
    const auto fill = [](kirpich::render::RampPalettes& set, std::size_t ramp) {
        const auto id = [ramp](int kind) {
            return static_cast<retropp::PaletteId>(1000 * kind + static_cast<int>(ramp));
        };
        set.font          = id(1);
        set.content       = id(2);
        set.fontDim       = id(3);
        set.fontSprite    = id(4);
        set.sprite0       = id(5);
        set.sprite1       = id(6);
        set.fontSpriteDim = id(7);
        set.spriteDim     = id(8);
    };
    for (std::size_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        fill(atlas.palettes[ramp], ramp);
    }
    // The custom set, numbered as the index that selects it, holding colors unlike any ramp's.
    fill(atlas.custom, kCustomRamp);
    atlas.customShades = ShadeRamp{.darkest  = {.r = 1, .g = 2, .b = 3},
                                   .dark     = {.r = 4, .g = 5, .b = 6},
                                   .light    = {.r = 7, .g = 8, .b = 9},
                                   .lightest = {.r = 10, .g = 11, .b = 12}};
    return atlas;
}

const TileAtlas kAtlas = makeAtlas();

// A set with no slot saved: the Palette screen as a player who never made a custom palette sees it.
const CustomPalettes kNoSlots{};

// Four distinct colors from one base, so palettes can be told apart.
ShadeRamp rampFrom(std::uint8_t base) {
    const auto c = [base](std::uint8_t step) {
        return retropp::Rgba8{.r = static_cast<std::uint8_t>(base + step),
                              .g = static_cast<std::uint8_t>(base + step + 1),
                              .b = static_cast<std::uint8_t>(base + step + 2)};
    };
    return ShadeRamp{.darkest = c(0), .dark = c(10), .light = c(20), .lightest = c(30)};
}

CustomPaletteSlot present(std::uint8_t base) {
    return CustomPaletteSlot{.present = true, .shades = rampFrom(base)};
}

// Slots 1, 4 and 7 saved.
CustomPalettes threeSlots() {
    CustomPalettes p;
    p.slots[0] = present(10);
    p.slots[3] = present(40);
    p.slots[6] = present(70);
    return p;
}

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held    = actionSet(as);
    game.audioCues      = kirpich::systems::AudioCues{};
}

struct Probe {
    Settings       settings{};
    CustomPalettes palettes{};
    int            applied        = 0;  // the settings seams
    int            saved          = 0;
    int            paletteApplies = 0;  // the palette seams
    int            paletteSaves   = 0;
    std::optional<ShadeRamp> lastApplied;
    std::vector<std::string> events;  // every seam, in the order it fired

    SettingsWiring wiring() {
        return SettingsWiring{
            .settings = &settings,
            .apply =
                [this](const Settings&) {
                    ++applied;
                    events.emplace_back("settings-apply");
                },
            .save =
                [this](const Settings&) {
                    ++saved;
                    events.emplace_back("settings-save");
                },
        };
    }

    CustomPaletteWiring custom() {
        return CustomPaletteWiring{
            .palettes = &palettes,
            .apply =
                [this](const ShadeRamp& shades) {
                    ++paletteApplies;
                    lastApplied = shades;
                    events.emplace_back("palette-apply");
                },
            .save =
                [this](const CustomPalettes&) {
                    ++paletteSaves;
                    events.emplace_back("palette-save");
                },
        };
    }
};

// The settings screen opened from the title screen, with the Palette screen opened from its row.
GameContext openPalette(const SettingsWiring& wiring) {
    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, wiring);
    game.screens.settingsRow = SettingsRow::PALETTE;
    press(game, {Action::Confirm});
    kirpich::systems::settingsScreen(game, wiring);
    return game;
}

kirpich::render::Layers draw(const Settings& s, const CustomPalettes& palettes,
                             bool blinkOn = true) {
    return kirpich::render::PaletteSettingsScreen(s, palettes, blinkOn, kAtlas);
}

kirpich::render::Layers draw(std::uint8_t ramp, bool blinkOn = true) {
    return draw(Settings{.shadeRamp = ramp}, kNoSlots, blinkOn);
}

std::span<const retropp::Sprite> contentOf(const kirpich::render::Layers& layers) {
    return std::get<retropp::SpriteContent>(layers[1].content).sprites;
}

std::optional<retropp::Sprite> named(std::span<const retropp::Sprite> sprites,
                                     std::string_view key) {
    for (const retropp::Sprite& s : sprites) {
        if (s.key.value == key) return s;
    }
    return std::nullopt;
}

std::string glyphKey(int x, int y) {
    return "glyph-" + std::to_string(x) + "-" + std::to_string(y);
}

// The value the row reads, glyph by glyph from the value column, checked against what Glyphs would
// draw for `text` there and asserted to stop where the text does.
void expectValue(std::span<const retropp::Sprite> content, std::string_view text,
                 std::uint8_t index) {
    const auto expected = kirpich::render::Glyphs(text, kValueX, kLineY, 8, kAtlas, index);
    for (const retropp::Sprite& want : expected) {
        const auto got = named(content, want.key.value);
        ASSERT_TRUE(got) << "value " << text;
        EXPECT_EQ(got->tile, want.tile) << "value " << text;
        EXPECT_EQ(got->palette, want.palette) << "value " << text;
    }
    EXPECT_FALSE(named(content, glyphKey(kValueX + 8 * static_cast<int>(text.size()), kLineY)))
        << "value " << text << " is followed by another glyph";
}

// The sprites of one piece in the preview, by kind.
std::vector<retropp::Sprite> pieceSprites(std::span<const retropp::Sprite> content,
                                          std::size_t kind) {
    const std::string stem = "palette-piece-" + std::to_string(kind) + "-";
    std::vector<retropp::Sprite> out;
    for (const retropp::Sprite& s : content) {
        if (s.key.value.starts_with(stem)) out.push_back(s);
    }
    return out;
}

struct Box {
    int left, top, right, bottom;
};

Box boxOf(const std::vector<retropp::Sprite>& parts) {
    Box b{1 << 20, 1 << 20, -(1 << 20), -(1 << 20)};
    for (const retropp::Sprite& s : parts) {
        b.left   = std::min(b.left, s.x);
        b.top    = std::min(b.top, s.y);
        b.right  = std::max(b.right, s.x + 8);
        b.bottom = std::max(b.bottom, s.y + 8);
    }
    return b;
}

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

TEST(PaletteSettingsScreen, OpeningShowsTheCursorAndArmsTheBlink) {
    GameContext game;
    game.screens.cursorVisible = false;

    kirpich::systems::openPaletteSettings(game);

    EXPECT_TRUE(game.screens.cursorVisible);
    EXPECT_EQ(game.flow.timer1, kBlinkFrames);
    EXPECT_EQ(game.flow.gameState, GameState::PALETTE_SETTINGS);
}

// Right steps to the next palette and left to the previous, every step cued and reaching both seams;
// a press past either end is an end stop and fires nothing. With no slot saved, the last ramp is the
// end of the walk.
TEST(PaletteSettingsScreen, EveryPaletteBothWaysWithEndStops) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    const auto  custom = probe.custom();
    GameContext game   = openPalette(wiring);
    ASSERT_EQ(game.flow.gameState, GameState::PALETTE_SETTINGS);

    for (std::size_t ramp = 0; ramp + 1 < kShadeRampCount; ++ramp) {
        press(game, {Action::MenuRight});
        kirpich::systems::paletteSettingsScreen(game, wiring, custom);
        EXPECT_EQ(probe.settings.shadeRamp, ramp + 1);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
        EXPECT_EQ(probe.applied, static_cast<int>(ramp + 1));
        EXPECT_EQ(probe.saved, static_cast<int>(ramp + 1));
    }
    const int appliedAtLast = probe.applied;
    press(game, {Action::MenuRight});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    EXPECT_EQ(probe.settings.shadeRamp, kShadeRampCount - 1);
    EXPECT_FALSE(probe.settings.customPalette);
    EXPECT_EQ(probe.applied, appliedAtLast) << "the last palette is an end stop";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);
    EXPECT_EQ(probe.paletteApplies, 0) << "no slot was ever stepped onto";

    for (std::size_t ramp = kShadeRampCount - 1; ramp > 0; --ramp) {
        press(game, {Action::MenuLeft});
        kirpich::systems::paletteSettingsScreen(game, wiring, custom);
        EXPECT_EQ(probe.settings.shadeRamp, ramp - 1);
    }
    const int appliedAtFirst = probe.applied;
    press(game, {Action::MenuLeft});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    EXPECT_EQ(probe.settings.shadeRamp, 0);
    EXPECT_EQ(probe.applied, appliedAtFirst) << "the first palette is an end stop";
}

// B goes back to the settings screen, painted again, with its cursor on the Palette row.
TEST(PaletteSettingsScreen, BackReturnsToTheSettingsScreenOnItsRow) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    const auto  custom = probe.custom();
    GameContext game   = openPalette(wiring);

    press(game, {Action::Back});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);

    EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::PALETTE);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    const std::size_t line = kirpich::systems::settingsRowLine(SettingsRow::PALETTE);
    EXPECT_EQ(game.display.map[line][kirpich::systems::kCursorCol],
              static_cast<std::uint8_t>(kirpich::CharTile::HYPHEN));
    EXPECT_EQ(probe.applied, 0) << "leaving changes nothing";
}

// Past the last ramp the selector walks on through the saved slots, in slot order, skipping the
// empty ones. Stepping onto a slot puts its palette to work - the working palette takes its colors,
// the atlas is told, the document is written, and the setting says a custom palette is in use - and
// stepping back off the first slot lands on the last ramp with the working palette left as it was.
TEST(PaletteSettingsScreen, TheSelectorWalksOnThroughThePresentSlots) {
    Probe probe;
    probe.palettes           = threeSlots();
    probe.settings.shadeRamp = kLastRamp;
    const auto  wiring = probe.wiring();
    const auto  custom = probe.custom();
    GameContext game   = openPalette(wiring);

    const auto step = [&](Action a) {
        press(game, {a});
        kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    };

    step(Action::MenuRight);  // 80 -> C01
    EXPECT_TRUE(probe.settings.customPalette);
    EXPECT_EQ(probe.settings.shadeRamp, kLastRamp) << "the ramp underneath is kept";
    EXPECT_EQ(probe.palettes.working, rampFrom(10));
    EXPECT_EQ(kirpich::savedSlotOf(probe.palettes), std::optional<std::uint8_t>{1});
    EXPECT_EQ(probe.paletteApplies, 1);
    EXPECT_EQ(probe.paletteSaves, 1);
    ASSERT_TRUE(probe.lastApplied);
    EXPECT_EQ(*probe.lastApplied, rampFrom(10));
    EXPECT_EQ(probe.applied, 1) << "the setting changed, so the settings seams fired";
    EXPECT_EQ(probe.saved, 1);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    step(Action::MenuRight);  // C01 -> C04, over the empty 2 and 3
    EXPECT_TRUE(probe.settings.customPalette);
    EXPECT_EQ(probe.palettes.working, rampFrom(40));
    EXPECT_EQ(kirpich::savedSlotOf(probe.palettes), std::optional<std::uint8_t>{4});
    EXPECT_EQ(probe.paletteApplies, 2);
    EXPECT_EQ(probe.paletteSaves, 2);
    EXPECT_EQ(probe.applied, 1) << "slot to slot changes no setting";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK) << "but it is still a move";

    step(Action::MenuRight);  // C04 -> C07
    EXPECT_EQ(kirpich::savedSlotOf(probe.palettes), std::optional<std::uint8_t>{7});
    EXPECT_EQ(probe.paletteApplies, 3);

    step(Action::MenuRight);  // C07 is the end of the walk
    EXPECT_EQ(kirpich::savedSlotOf(probe.palettes), std::optional<std::uint8_t>{7});
    EXPECT_EQ(probe.paletteApplies, 3) << "an end stop fires nothing";
    EXPECT_EQ(probe.paletteSaves, 3);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    step(Action::MenuLeft);  // C07 -> C04
    EXPECT_EQ(kirpich::savedSlotOf(probe.palettes), std::optional<std::uint8_t>{4});
    EXPECT_EQ(probe.paletteApplies, 4);
    step(Action::MenuLeft);  // C04 -> C01
    EXPECT_EQ(kirpich::savedSlotOf(probe.palettes), std::optional<std::uint8_t>{1});
    EXPECT_EQ(probe.paletteApplies, 5);

    step(Action::MenuLeft);  // C01 -> 80
    EXPECT_FALSE(probe.settings.customPalette);
    EXPECT_EQ(probe.settings.shadeRamp, kLastRamp);
    EXPECT_EQ(probe.palettes.working, rampFrom(10)) << "the working palette is left where it was";
    EXPECT_EQ(probe.paletteApplies, 5) << "leaving the slots touches neither palette seam";
    EXPECT_EQ(probe.paletteSaves, 5);
    EXPECT_EQ(probe.applied, 2) << "the setting changed back";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    step(Action::MenuLeft);  // 80 -> 79, as it always did
    EXPECT_EQ(probe.settings.shadeRamp, kLastRamp - 1);
    EXPECT_FALSE(probe.settings.customPalette);
}

// A custom palette in use that is saved in no slot reads `C` and stands off the walk: Left goes to
// the last ramp, whatever ramp byte lies underneath, and Right to the first saved slot.
TEST(PaletteSettingsScreen, AnUnsavedCustomPaletteReadsCAndStepsBackOntoTheWalk) {
    Probe probe;
    probe.palettes          = threeSlots();
    probe.palettes.working  = rampFrom(55);  // in no slot
    probe.settings          = Settings{.shadeRamp = 12, .customPalette = true};
    const auto  wiring = probe.wiring();
    const auto  custom = probe.custom();
    GameContext game   = openPalette(wiring);

    EXPECT_FALSE(kirpich::savedSlotOf(probe.palettes));
    expectValue(contentOf(draw(probe.settings, probe.palettes)), "C", kCustomRamp);

    press(game, {Action::MenuLeft});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    EXPECT_FALSE(probe.settings.customPalette);
    EXPECT_EQ(probe.settings.shadeRamp, kLastRamp) << "off the walk, Left lands on the last ramp";
    EXPECT_EQ(probe.palettes.working, rampFrom(55)) << "the working palette is kept";
    EXPECT_EQ(probe.paletteApplies, 0);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    probe.settings = Settings{.shadeRamp = 12, .customPalette = true};
    press(game, {Action::MenuRight});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    EXPECT_TRUE(probe.settings.customPalette);
    EXPECT_EQ(kirpich::savedSlotOf(probe.palettes), std::optional<std::uint8_t>{1})
        << "off the walk, Right lands on the first saved slot";
    EXPECT_EQ(probe.paletteApplies, 1);
}

// The value reads `C` and the saved slot as two digits, counted from one, at the value column and
// nothing after it; a slot under ten keeps its leading zero.
TEST(PaletteSettingsScreen, TheValueReadsTheSlotAsTwoDigits) {
    CustomPalettes p = threeSlots();
    const Settings s{.shadeRamp = kPlayerRamp, .customPalette = true};

    p.working = p.slots[0].shades;
    expectValue(contentOf(draw(s, p)), "C01", kCustomRamp);

    p.slots[46] = present(90);
    p.working   = p.slots[46].shades;
    expectValue(contentOf(draw(s, p)), "C47", kCustomRamp);

    p.slots[98] = present(100);
    p.working   = p.slots[98].shades;
    expectValue(contentOf(draw(s, p)), "C99", kCustomRamp);

    // With the custom palette not in use the row reads the ramp's number, whatever the slots hold.
    expectValue(contentOf(draw(Settings{.shadeRamp = kPlayerRamp}, p)), "8", kPlayerRamp);
}

// An arrow stands exactly where a press would move: none to the left of the first ramp; both on the
// last ramp while a slot is saved and none to its right otherwise; both on a slot with slots either
// side; none to the right of the last saved slot; both off the walk.
TEST(PaletteSettingsScreen, ArrowsAgreeWithTheWalkAtEveryStop) {
    const auto arrows = [](const Settings& s, const CustomPalettes& p) {
        const auto content = contentOf(draw(s, p));
        return std::pair{named(content, "scroller-5-left").has_value(),
                         named(content, "scroller-5-right").has_value()};
    };
    const auto canStep = [](const Settings& s, const CustomPalettes& p) {
        return std::pair{kirpich::systems::paletteCanStep(s, &p, -1),
                         kirpich::systems::paletteCanStep(s, &p, +1)};
    };
    const CustomPalettes three = threeSlots();

    struct Case {
        const char*    name;
        Settings       settings;
        CustomPalettes palettes;
        bool           left;
        bool           right;
    };
    CustomPalettes onFirst = three;
    onFirst.working        = three.slots[0].shades;
    CustomPalettes onLast  = three;
    onLast.working         = three.slots[6].shades;
    CustomPalettes offWalk = three;
    offWalk.working        = rampFrom(55);

    const std::array<Case, 7> cases{{
        {"first ramp", Settings{.shadeRamp = 0}, kNoSlots, false, true},
        {"a middle ramp", Settings{.shadeRamp = 3}, kNoSlots, true, true},
        {"last ramp, no slots", Settings{.shadeRamp = kLastRamp}, kNoSlots, true, false},
        {"last ramp, slots saved", Settings{.shadeRamp = kLastRamp}, three, true, true},
        {"first slot", Settings{.shadeRamp = kLastRamp, .customPalette = true}, onFirst, true, true},
        {"last slot", Settings{.shadeRamp = kLastRamp, .customPalette = true}, onLast, true, false},
        {"off the walk", Settings{.shadeRamp = 2, .customPalette = true}, offWalk, true, true},
    }};

    for (const Case& c : cases) {
        EXPECT_EQ(canStep(c.settings, c.palettes), (std::pair{c.left, c.right})) << c.name;
        EXPECT_EQ(arrows(c.settings, c.palettes), (std::pair{c.left, c.right})) << c.name;
    }

    // The arrows' own cells, and the left one flipped, unchanged by the walk growing.
    const auto content = contentOf(draw(3));
    const auto left    = named(content, "scroller-5-left");
    const auto right   = named(content, "scroller-5-right");
    ASSERT_TRUE(left && right);
    EXPECT_EQ(left->x, kLeftArrowX);
    EXPECT_TRUE(left->flipX);
    EXPECT_EQ(right->x, kRightArrowX);
}

// While a custom palette is in use every part of the screen draws through the atlas's custom set and
// the swatch shows the set's current colors; the frame the setting clears, it is the ramp's again.
TEST(PaletteSettingsScreen, TheScreenDrawsThroughTheCustomSetWhileItIsInUse) {
    CustomPalettes p = threeSlots();
    p.working        = p.slots[3].shades;

    const auto check = [&p](const Settings& s, const kirpich::render::RampPalettes& set,
                            std::span<const retropp::Rgba8, 4> colors, const char* what) {
        const auto layers = draw(s, p);

        const auto& backdrop = std::get<retropp::TileContent>(layers[0].content).cells;
        ASSERT_FALSE(backdrop.empty()) << what;
        EXPECT_EQ(backdrop[0].palette, set.content) << what << ": the backdrop";

        const auto content = contentOf(layers);
        const auto heading = named(content, glyphKey(kirpich::render::palette_settings::kHeadingX,
                                                     2 * 8));
        ASSERT_TRUE(heading) << what;
        EXPECT_EQ(heading->palette, set.fontSprite) << what << ": the heading";

        const auto piece = pieceSprites(content, 0);
        ASSERT_FALSE(piece.empty()) << what;
        EXPECT_EQ(piece.front().palette, set.sprite0) << what << ": a piece";

        const auto& swatch = layers[1].regions;
        ASSERT_EQ(swatch.size(), 4u) << what;
        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_EQ(swatch[i].effects[0].fill.r, colors[i].r) << what << ": square " << i;
            EXPECT_EQ(swatch[i].effects[0].fill.g, colors[i].g) << what << ": square " << i;
            EXPECT_EQ(swatch[i].effects[0].fill.b, colors[i].b) << what << ": square " << i;
        }
    };

    const std::array<retropp::Rgba8, 4> customColors{
        kAtlas.customShades.darkest, kAtlas.customShades.dark, kAtlas.customShades.light,
        kAtlas.customShades.lightest};
    check(Settings{.shadeRamp = kPlayerRamp, .customPalette = true}, kAtlas.custom, customColors,
          "custom in use");

    const auto rampColors = kirpich::render::rampColours(kPlayerRamp);
    check(Settings{.shadeRamp = kPlayerRamp, .customPalette = false}, kAtlas.palettes[kPlayerRamp],
          rampColors, "the ramp again");
}

// With no palettes behind the wiring the screen is exactly what it was before slots existed: the last
// ramp is an end stop, the value is the ramp's number, and a setting that names the custom palette
// steps back onto the last ramp but never forward.
TEST(PaletteSettingsScreen, ANullPaletteWiringLeavesTheScreenAsItWas) {
    Probe probe;
    probe.settings.shadeRamp = kLastRamp;
    const auto                wiring = probe.wiring();
    const CustomPaletteWiring none{};
    GameContext               game = openPalette(wiring);

    press(game, {Action::MenuRight});
    kirpich::systems::paletteSettingsScreen(game, wiring, none);
    EXPECT_EQ(probe.settings.shadeRamp, kLastRamp);
    EXPECT_FALSE(probe.settings.customPalette);
    EXPECT_EQ(probe.applied, 0);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    const Settings custom{.shadeRamp = 12, .customPalette = true};
    EXPECT_FALSE(kirpich::systems::paletteCanStep(custom, nullptr, +1));
    EXPECT_TRUE(kirpich::systems::paletteCanStep(custom, nullptr, -1));

    probe.settings = custom;
    press(game, {Action::MenuLeft});
    kirpich::systems::paletteSettingsScreen(game, wiring, none);
    EXPECT_FALSE(probe.settings.customPalette);
    EXPECT_EQ(probe.settings.shadeRamp, kLastRamp);

    expectValue(contentOf(draw(kLastRamp)), "80", kLastRamp);
}

// Stepping onto a slot tells the atlas, then writes the document, then stores the setting - in that
// order, so the picture never names colors the set does not yet hold; a step between two slots
// touches the palette seams only.
TEST(PaletteSettingsScreen, SteppingOntoASlotAppliesThenSavesThenStoresTheSetting) {
    Probe probe;
    probe.palettes           = threeSlots();
    probe.settings.shadeRamp = kLastRamp;
    const auto  wiring = probe.wiring();
    const auto  custom = probe.custom();
    GameContext game   = openPalette(wiring);
    probe.events.clear();

    press(game, {Action::MenuRight});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    EXPECT_EQ(probe.events, (std::vector<std::string>{"palette-apply", "palette-save",
                                                      "settings-apply", "settings-save"}));

    probe.events.clear();
    press(game, {Action::MenuRight});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    EXPECT_EQ(probe.events, (std::vector<std::string>{"palette-apply", "palette-save"}));
}

// Down opens the palette editor with the screen-change cue and the blink armed, its preset on the
// slot the working palette is saved in - or on slot 1 while it is saved in none, or with no palettes
// behind the wiring.
TEST(PaletteSettingsScreen, DownOpensThePaletteEditor) {
    const auto openWith = [](const CustomPalettes& palettes, bool wired) {
        Probe probe;
        probe.palettes    = palettes;
        const auto wiring = probe.wiring();
        const auto custom = wired ? probe.custom() : CustomPaletteWiring{};
        GameContext game  = openPalette(wiring);
        game.flow.timer1  = 3;
        press(game, {Action::MenuDown});
        kirpich::systems::paletteSettingsScreen(game, wiring, custom);
        return game;
    };

    CustomPalettes saved = threeSlots();
    saved.working        = saved.slots[3].shades;
    const GameContext a  = openWith(saved, true);
    EXPECT_EQ(a.flow.gameState, GameState::PALETTE_EDITOR);
    EXPECT_EQ(a.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    EXPECT_EQ(a.flow.timer1, kBlinkFrames);
    EXPECT_TRUE(a.screens.cursorVisible);
    EXPECT_EQ(a.paletteEditor.preset, 4);

    CustomPalettes unsaved = threeSlots();
    unsaved.working        = rampFrom(99);
    EXPECT_EQ(openWith(unsaved, true).paletteEditor.preset, 1);

    const GameContext none = openWith(saved, false);
    EXPECT_EQ(none.flow.gameState, GameState::PALETTE_EDITOR);
    EXPECT_EQ(none.paletteEditor.preset, 1);
}

// The down arrow says the editor is below: always drawn, at the shared page-arrow place, turned
// down, through the palette in use - and there is no up arrow.
TEST(PaletteSettingsScreen, TheDownArrowIsAlwaysShown) {
    CustomPalettes p = threeSlots();
    p.working        = p.slots[0].shades;
    for (const auto& [s, palettes] :
         {std::pair{Settings{.shadeRamp = kPlayerRamp}, kNoSlots},
          std::pair{Settings{.shadeRamp = kPlayerRamp, .customPalette = true}, p}}) {
        const auto content = contentOf(draw(s, palettes));
        const auto down    = named(content, "palette-down");
        ASSERT_TRUE(down);
        EXPECT_EQ(down->x, 80);
        EXPECT_EQ(down->y, 128);
        EXPECT_EQ(down->rotation, retropp::Rotation::Rot90);
        EXPECT_EQ(down->atlas, kAtlas.copyrightTitle);
        EXPECT_EQ(down->palette, kirpich::render::palettesFor(kAtlas, kirpich::paletteIndexOf(s)).sprite0);
        EXPECT_FALSE(named(content, "palette-up"));
    }
}

// ── The components ────────────────────────────────────────────────────────────────────────────────

// The palette's number is counted from one and starts on the value column, one digit or two. The
// row keeps its "palette" label, the heading names the screen, and the cursor marks the row while
// the blink is on.
TEST(PaletteSettingsScreen, TheRowReadsTheNumberFromTheValueColumn) {
    for (std::uint8_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        const auto content = contentOf(draw(ramp));
        const std::string number = std::to_string(ramp + 1);
        const auto expected = kirpich::render::Glyphs(number, kValueX, kLineY, 8, kAtlas, ramp);
        for (const retropp::Sprite& want : expected) {
            const auto got = named(content, want.key.value);
            ASSERT_TRUE(got) << "palette " << number;
            EXPECT_EQ(got->tile, want.tile) << "palette " << number;
        }
        EXPECT_FALSE(named(content, glyphKey(kValueX + 8 * static_cast<int>(number.size()), kLineY)))
            << "palette " << number << " is followed by another glyph";
    }

    const auto content = contentOf(draw(kPlayerRamp));
    const auto label   = kirpich::render::Glyphs("palette", 3 * 8, kLineY, 8, kAtlas, kPlayerRamp);
    for (const retropp::Sprite& want : label) {
        EXPECT_TRUE(named(content, want.key.value)) << "the row's label";
    }
    EXPECT_TRUE(named(content, glyphKey(kCursorX, kLineY))) << "the cursor, blink on";
    EXPECT_FALSE(named(contentOf(draw(kPlayerRamp, false)), glyphKey(kCursorX, kLineY)))
        << "the cursor, blink off";
    EXPECT_TRUE(named(content, glyphKey(kirpich::render::palette_settings::kHeadingX, 2 * 8)))
        << "the heading";
}

// An arrow stands only where the palette can still step: none to the left of the first, none to the
// right of the last with no slot saved, both between.
TEST(PaletteSettingsScreen, ArrowsStandOnlyWhereThePaletteCanStep) {
    const auto arrows = [](std::uint8_t ramp) {
        const auto content = contentOf(draw(ramp));
        return std::pair{named(content, "scroller-5-left"), named(content, "scroller-5-right")};
    };

    auto [left, right] = arrows(0);
    EXPECT_FALSE(left);
    ASSERT_TRUE(right);
    EXPECT_EQ(right->x, kRightArrowX);

    std::tie(left, right) = arrows(3);
    ASSERT_TRUE(left);
    EXPECT_TRUE(right);
    EXPECT_EQ(left->x, kLeftArrowX);
    EXPECT_TRUE(left->flipX);

    std::tie(left, right) = arrows(static_cast<std::uint8_t>(kShadeRampCount - 1));
    EXPECT_TRUE(left);
    EXPECT_FALSE(right);
}

// The swatch is the palette's four colors in shade order, opaque, abutting, and centered across the
// screen on the line under the palette row - riding the content layer as its regions. Swept over
// every palette, so one whose colors were routed wrong fails here.
TEST(PaletteSettingsScreen, TheSwatchIsThePalettesFourColorsAbuttingAndCentered) {
    for (std::uint8_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        const auto layers  = draw(ramp);
        const auto colors  = kirpich::render::rampColours(ramp);
        const auto& swatch = layers[1].regions;

        ASSERT_EQ(swatch.size(), colors.size()) << "palette " << +ramp;
        for (std::size_t i = 0; i < swatch.size(); ++i) {
            const retropp::Region& square = swatch[i];
            ASSERT_EQ(square.effects.size(), 1u);
            EXPECT_EQ(square.effects[0].kind, retropp::ScreenSpaceEffectKind::ColorFill);
            EXPECT_EQ(square.effects[0].fill.r, colors[i].r) << "palette " << +ramp;
            EXPECT_EQ(square.effects[0].fill.g, colors[i].g) << "palette " << +ramp;
            EXPECT_EQ(square.effects[0].fill.b, colors[i].b) << "palette " << +ramp;
            EXPECT_EQ(square.effects[0].fill.a, 255) << "a swatch square is opaque";
            EXPECT_FLOAT_EQ(square.shape.points[0].y, 6.0f * 8) << "the line under the row";
            if (i > 0) {
                EXPECT_FLOAT_EQ(square.shape.points[0].x, swatch[i - 1].shape.points[1].x)
                    << "square " << i << " must touch the last";
            }
        }
        const float left  = swatch.front().shape.points[0].x;
        const float right = swatch.back().shape.points[1].x;
        EXPECT_FLOAT_EQ(left, kScreenWidth - right) << "centered";
        EXPECT_FLOAT_EQ(right - left, 32.0f);
    }
}

// The pieces are drawn through the palette the player has chosen, not through the one the screen
// was opened with or the default.
TEST(PaletteSettingsScreen, ThePiecesDrawThroughTheChosenPalette) {
    const auto content = contentOf(draw(kPlayerRamp));
    const auto other   = kirpich::render::PieceShape(kirpich::PieceKind::T, 0, 0, 0, "x", kAtlas, 0);

    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        const auto drawn  = pieceSprites(content, kind);
        const auto chosen = kirpich::render::PieceShape(static_cast<kirpich::PieceKind>(kind), 0, 0,
                                                        0, "x", kAtlas, kPlayerRamp);
        ASSERT_EQ(drawn.size(), chosen.size()) << "kind " << kind;
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            EXPECT_EQ(drawn[i].palette, chosen[i].palette) << "kind " << kind;
            EXPECT_NE(drawn[i].palette, other.front().palette)
                << "kind " << kind << " is drawn through the first palette";
        }
    }
}

// All seven are on the screen, in PieceKind order across two rows of four and three, each row on
// its own top, centered across the screen, and no two overlapping.
TEST(PaletteSettingsScreen, TheSevenPiecesAreAllOnScreenInTwoCenteredRows) {
    const auto content = contentOf(draw(kPlayerRamp));
    namespace layout   = kirpich::render::palette_settings;

    std::vector<Box> boxes;
    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        const auto parts = pieceSprites(content, kind);
        ASSERT_FALSE(parts.empty()) << "kind " << kind << " is missing";
        boxes.push_back(boxOf(parts));

        const Box& b = boxes.back();
        EXPECT_GE(b.left, 0);
        EXPECT_GE(b.top, 0);
        EXPECT_LE(b.right, kScreenWidth);
        EXPECT_LE(b.bottom, kScreenHeight);

        const auto extent = kirpich::render::pieceShapeExtent(static_cast<kirpich::PieceKind>(kind));
        EXPECT_EQ(b.right - b.left, extent.width) << "kind " << kind;
        EXPECT_EQ(b.bottom - b.top, extent.height) << "kind " << kind;
    }

    const auto checkRow = [&boxes](std::size_t first, std::size_t last, int top) {
        for (std::size_t k = first; k < last; ++k) {
            EXPECT_EQ(boxes[k].top, top) << "kind " << k;
            if (k > first) {
                EXPECT_EQ(boxes[k].left, boxes[k - 1].right + 8)
                    << "kind " << k << " sits a cell after the one before it";
            }
        }
        const int left  = boxes[first].left;
        const int right = boxes[last - 1].right;
        EXPECT_LE(std::abs(left - (kScreenWidth - right)), 1) << "the row starting " << first;
    };
    checkRow(0, 4, kirpich::render::kPiecePreviewFirstRowY);
    checkRow(4, kirpich::kPieceKindCount, kirpich::render::kPiecePreviewSecondRowY);

    for (std::size_t a = 0; a < boxes.size(); ++a) {
        for (std::size_t b = a + 1; b < boxes.size(); ++b) {
            const bool apart = boxes[a].right <= boxes[b].left || boxes[b].right <= boxes[a].left ||
                               boxes[a].bottom <= boxes[b].top || boxes[b].bottom <= boxes[a].top;
            EXPECT_TRUE(apart) << "kinds " << a << " and " << b << " overlap";
        }
    }
}

// Within one frame no two objects share a name, which is what the renderer requires.
TEST(PaletteSettingsScreen, NoTwoObjectsInAFrameShareAName) {
    for (const std::uint8_t ramp : {std::uint8_t{0}, kPlayerRamp,
                                    static_cast<std::uint8_t>(kShadeRampCount - 1)}) {
        std::vector<std::string> keys;
        for (const retropp::Sprite& s : contentOf(draw(ramp))) {
            keys.push_back(s.key.value);
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            for (std::size_t j = i + 1; j < keys.size(); ++j) {
                EXPECT_NE(keys[i], keys[j]) << "two objects are named " << keys[i];
            }
        }
    }
}
