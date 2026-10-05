// The Palette settings screen — behavioral tests over its logic (src/systems/palette_settings_screen.h)
// and its components (src/render/palette_settings/, src/render/piece_shape.h).
//
// Device-free. The logic is pure state over the game aggregate and the wiring's seams, and the
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

#include "render/glyphs.h"
#include "render/palette_settings/layout.h"
#include "render/palette_settings/screen.h"
#include "render/palettes.h"
#include "render/piece_shape.h"
#include "retropp/input.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/palette_settings_screen.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::GameState;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::render::kShadeRampCount;
using kirpich::render::TileAtlas;
using kirpich::systems::GameContext;
using kirpich::systems::SettingsWiring;

constexpr std::uint8_t kPlayerRamp = 7;

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
    for (std::size_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        const auto id = [ramp](int kind) {
            return static_cast<retropp::PaletteId>(1000 * kind + static_cast<int>(ramp));
        };
        atlas.palettes[ramp].font          = id(1);
        atlas.palettes[ramp].content       = id(2);
        atlas.palettes[ramp].fontDim       = id(3);
        atlas.palettes[ramp].fontSprite    = id(4);
        atlas.palettes[ramp].sprite0       = id(5);
        atlas.palettes[ramp].sprite1       = id(6);
        atlas.palettes[ramp].fontSpriteDim = id(7);
        atlas.palettes[ramp].spriteDim     = id(8);
    }
    return atlas;
}

const TileAtlas kAtlas = makeAtlas();

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
    Settings settings{};
    int      applied = 0;
    int      saved   = 0;

    SettingsWiring wiring() {
        return SettingsWiring{
            .settings = &settings,
            .apply    = [this](const Settings&) { ++applied; },
            .save     = [this](const Settings&) { ++saved; },
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

kirpich::render::Layers draw(std::uint8_t ramp, bool blinkOn = true) {
    return kirpich::render::PaletteSettingsScreen(Settings{.shadeRamp = ramp}, blinkOn, kAtlas);
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
// a press past either end is an end stop and fires nothing.
TEST(PaletteSettingsScreen, EveryPaletteBothWaysWithEndStops) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    GameContext game   = openPalette(wiring);
    ASSERT_EQ(game.flow.gameState, GameState::PALETTE_SETTINGS);

    for (std::size_t ramp = 0; ramp + 1 < kShadeRampCount; ++ramp) {
        press(game, {Action::MenuRight});
        kirpich::systems::paletteSettingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.shadeRamp, ramp + 1);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
        EXPECT_EQ(probe.applied, static_cast<int>(ramp + 1));
        EXPECT_EQ(probe.saved, static_cast<int>(ramp + 1));
    }
    const int appliedAtLast = probe.applied;
    press(game, {Action::MenuRight});
    kirpich::systems::paletteSettingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.shadeRamp, kShadeRampCount - 1);
    EXPECT_EQ(probe.applied, appliedAtLast) << "the last palette is an end stop";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    for (std::size_t ramp = kShadeRampCount - 1; ramp > 0; --ramp) {
        press(game, {Action::MenuLeft});
        kirpich::systems::paletteSettingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.shadeRamp, ramp - 1);
    }
    const int appliedAtFirst = probe.applied;
    press(game, {Action::MenuLeft});
    kirpich::systems::paletteSettingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.shadeRamp, 0);
    EXPECT_EQ(probe.applied, appliedAtFirst) << "the first palette is an end stop";
}

// B goes back to the settings screen, painted again, with its cursor on the Palette row.
TEST(PaletteSettingsScreen, BackReturnsToTheSettingsScreenOnItsRow) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    GameContext game   = openPalette(wiring);

    press(game, {Action::Back});
    kirpich::systems::paletteSettingsScreen(game, wiring);

    EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::PALETTE);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    const std::size_t line = kirpich::systems::settingsRowLine(SettingsRow::PALETTE);
    EXPECT_EQ(game.display.map[line][kirpich::systems::kCursorCol],
              static_cast<std::uint8_t>(kirpich::CharTile::HYPHEN));
    EXPECT_EQ(probe.applied, 0) << "leaving changes nothing";
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
// right of the last, both between.
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
    checkRow(0, 4, layout::kPreviewFirstRowY);
    checkRow(4, kirpich::kPieceKindCount, layout::kPreviewSecondRowY);

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
