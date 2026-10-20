// The palette editor's held preview - behavioral tests over the Select level in its logic
// (src/render/screens/palette_editor/palette_editor.h), the two components the preview is drawn from
// (preview_backdrop.h and palette_preview.h beside it), the shared piece preview they reuse
// (src/render/components/piece_preview.h), and the screen's declaration while the preview is up
// (screen.h).
//
// Device-free. The logic is pure state over the game aggregate and the wirings' seams, and the
// components are pure functions returning primitives, so the atlas here is a plain value with
// distinguishable handles rather than anything uploaded. The preview is the port's own, so every
// asserted value comes from its stated contract.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/piece_kind.h>

#include "data/tilemaps.h"
#include "data/type_c_tilemap.h"
#include "render/background.h"
#include "render/components/backdrop.h"
#include "render/components/glyphs.h"
#include "render/components/piece_preview.h"
#include "render/palettes.h"
#include "render/screens/palette_editor/layout.h"
#include "render/screens/palette_editor/palette_editor.h"
#include "render/screens/palette_editor/palette_preview.h"
#include "render/screens/palette_editor/preview_backdrop.h"
#include "render/screens/palette_editor/screen.h"
#include "render/tile_atlas.h"
#include "retropp/input.h"
#include "state/custom_palettes.h"
#include "state/playing_field_state.h"
#include "state/screen_ui_state.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/palette_settings_screen.h"
#include "systems/settings_screen.h"

namespace {

namespace layout = kirpich::render::palette_editor;

using kirpich::Action;
using kirpich::ColorChannel;
using kirpich::CustomPalettes;
using kirpich::CustomPaletteSlot;
using kirpich::DigitPlace;
using kirpich::GameState;
using kirpich::PaletteEditorQuestion;
using kirpich::PaletteEditorRow;
using kirpich::PaletteEditorState;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::SquareSfxId;
using kirpich::render::kCustomRamp;
using kirpich::render::kShadeRampCount;
using kirpich::render::ShadeRamp;
using kirpich::render::TileAtlas;
using kirpich::systems::CustomPaletteWiring;
using kirpich::systems::GameContext;
using kirpich::systems::SettingsWiring;

constexpr std::uint8_t kPlayerRamp  = 7;
constexpr std::uint8_t kBlinkFrames = 16;

TileAtlas makeAtlas() {
    TileAtlas atlas{
        .font             = static_cast<retropp::AtlasId>(11),
        .copyrightTitle   = static_cast<retropp::AtlasId>(22),
        .gameplay         = static_cast<retropp::AtlasId>(33),
        .multiplayerBuran = static_cast<retropp::AtlasId>(44),
        .glyphExtras      = static_cast<retropp::AtlasId>(55),
    };
    const auto fill = [](kirpich::render::RampPalettes& set, std::size_t ramp) {
        const auto id = [ramp](int kind) {
            return static_cast<retropp::PaletteId>(1000 * kind + static_cast<int>(ramp));
        };
        set.font               = id(1);
        set.content            = id(2);
        set.fontDim            = id(3);
        set.fontSprite         = id(4);
        set.sprite0            = id(5);
        set.sprite1            = id(6);
        set.fontSpriteDim      = id(7);
        set.spriteDim          = id(8);
        set.fontSpriteSelected = id(9);
    };
    for (std::size_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        fill(atlas.palettes[ramp], ramp);
    }
    fill(atlas.custom, kCustomRamp);
    return atlas;
}

const TileAtlas kAtlas = makeAtlas();

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

// The working palette saved in slot 4, so it has no unsaved changes.
CustomPalettes saved() {
    CustomPalettes p = threeSlots();
    p.working        = p.slots[3].shades;
    return p;
}

// The working palette in no slot and edited, so it has unsaved changes.
CustomPalettes unsaved() {
    CustomPalettes p = threeSlots();
    p.working        = rampFrom(99);
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

void hold(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = retropp::ActionSet{};
    game.joypad.held    = actionSet(as);
    game.audioCues      = kirpich::systems::AudioCues{};
}

struct Probe {
    Settings       settings{.shadeRamp = kPlayerRamp};
    CustomPalettes palettes{};
    int            applies = 0;
    int            saves   = 0;

    SettingsWiring wiring() { return SettingsWiring{.settings = &settings}; }

    CustomPaletteWiring custom() {
        return CustomPaletteWiring{
            .palettes = &palettes,
            .apply    = [this](const ShadeRamp&) { ++applies; },
            .save     = [this](const CustomPalettes&) { ++saves; },
        };
    }
};

// The editor opened the way a player opens it: the settings screen from the title, the Palette
// screen from its row, and Down.
GameContext openEditor(const SettingsWiring& wiring, const CustomPaletteWiring& custom) {
    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, wiring);
    game.screens.settingsRow = SettingsRow::PALETTE;
    press(game, {Action::Confirm});
    kirpich::systems::settingsScreen(game, wiring);
    press(game, {Action::MenuDown});
    kirpich::systems::paletteSettingsScreen(game, wiring, custom);
    return game;
}

struct Session {
    Probe&              probe;
    SettingsWiring      wiring;
    CustomPaletteWiring custom;
    GameContext         game;

    explicit Session(Probe& p)
        : probe(p), wiring(p.wiring()), custom(p.custom()), game(openEditor(wiring, custom)) {}

    void step(std::initializer_list<Action> as) {
        press(game, as);
        kirpich::systems::paletteEditor(game, wiring, custom);
    }
    void holdFor(std::initializer_list<Action> as) {
        hold(game, as);
        kirpich::systems::paletteEditor(game, wiring, custom);
    }
    PaletteEditorState& ui() { return game.paletteEditor; }
    SquareSfxId         sound() const { return game.audioCues.square; }

    // Walk the cursor to `row` from the preset row.
    void toRow(PaletteEditorRow row) {
        while (ui().row != row) step({Action::MenuDown});
    }
};

kirpich::render::Layers draw(const PaletteEditorState& ui, const CustomPalettes& palettes,
                             bool blinkOn = false, Settings s = Settings{.shadeRamp = kPlayerRamp}) {
    return kirpich::render::PaletteEditorScreen(ui, s, palettes, blinkOn, kAtlas);
}

std::span<const retropp::Sprite> spritesOf(const kirpich::render::Layers& layers) {
    return std::get<retropp::SpriteContent>(layers[1].content).sprites;
}

std::span<const retropp::TileCell> cellsOf(const kirpich::render::Layers& layers) {
    return std::get<retropp::TileContent>(layers[0].content).cells;
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

// Every sprite of `expected` is in `content` under its own name, drawing the same tile from the same
// sheet through the same palette at the same pixel.
void expectRun(std::span<const retropp::Sprite> content, const kirpich::render::Sprites& expected,
               std::string_view what) {
    ASSERT_FALSE(expected.empty()) << what;
    for (const retropp::Sprite& want : expected) {
        const auto got = named(content, want.key.value);
        ASSERT_TRUE(got) << what << " at " << want.key.value;
        EXPECT_EQ(got->x, want.x) << what;
        EXPECT_EQ(got->y, want.y) << what;
        EXPECT_EQ(got->tile, want.tile) << what;
        EXPECT_EQ(got->atlas, want.atlas) << what;
        EXPECT_EQ(got->palette, want.palette) << what;
    }
}

// Two cells name the same picture: the same sheet, cell, palette, flips and rotation.
bool sameCell(const retropp::TileCell& a, const retropp::TileCell& b) {
    return a.atlas == b.atlas && a.tile == b.tile && a.palette == b.palette && a.flipX == b.flipX &&
           a.flipY == b.flipY && a.rotation == b.rotation;
}

void expectCellsEqual(std::span<const retropp::TileCell> got,
                      std::span<const retropp::TileCell> want) {
    ASSERT_EQ(got.size(), want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_TRUE(sameCell(got[i], want[i])) << "cell " << i;
    }
}

// The custom set's nine palettes.
std::set<retropp::PaletteId> customHandles() {
    const auto& c = kAtlas.custom;
    return {c.font,          c.content,       c.fontDim,   c.fontSprite,        c.sprite0,
            c.sprite1,       c.fontSpriteDim, c.spriteDim, c.fontSpriteSelected};
}

// The kind a `preview-piece-<kind>-<part>` sprite belongs to.
std::size_t kindOf(std::string_view key, std::string_view stem) {
    const std::string_view rest = key.substr(stem.size() + 1);
    return static_cast<std::size_t>(std::stoul(std::string(rest.substr(0, rest.find('-')))));
}

constexpr std::array kChannels{ColorChannel::RED, ColorChannel::GREEN, ColorChannel::BLUE};

constexpr std::array kNoPreviewKeys{
    std::string_view{"editor-up"},  std::string_view{"preset-left"},
    std::string_view{"preset-right"}, std::string_view{"value-left"},
    std::string_view{"value-right"}, std::string_view{"digit-up"},
    std::string_view{"digit-down"},
};

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

// Select held is the level: held alone, or pressed and held, puts the preview up; a frame with
// nothing held, or with another action held, puts it down.
TEST(PalettePreview, SelectHeldIsTheLevel) {
    Probe   probe;
    Session s(probe);
    ASSERT_EQ(s.game.flow.gameState, GameState::PALETTE_EDITOR);
    ASSERT_FALSE(s.ui().previewing);

    s.holdFor({Action::Select});
    EXPECT_TRUE(s.ui().previewing) << "held, not pressed";
    s.holdFor({});
    EXPECT_FALSE(s.ui().previewing);
    s.step({Action::Select});
    EXPECT_TRUE(s.ui().previewing) << "pressed and held";
    s.holdFor({Action::MenuLeft});
    EXPECT_FALSE(s.ui().previewing) << "another action held";
}

// While the preview is up no table runs, in any mode, and nothing is cued.
TEST(PalettePreview, NoTableRunsWhilePreviewing) {
    // A shade row.
    {
        Probe   probe;
        Session s(probe);
        s.toRow(PaletteEditorRow::SHADE_1);
        s.step({Action::Select, Action::Confirm});
        EXPECT_FALSE(s.ui().entering) << "A does not enter the value";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        s.step({Action::Select, Action::MenuDown});
        EXPECT_EQ(s.ui().row, PaletteEditorRow::SHADE_1) << "Down does not move the row";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        s.step({Action::Select, Action::Back});
        EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_EDITOR) << "B does not leave";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    }

    // The preset row.
    {
        Probe probe;
        probe.palettes = threeSlots();
        Session s(probe);
        ASSERT_EQ(s.ui().row, PaletteEditorRow::PRESET);
        const std::uint8_t preset = s.ui().preset;
        s.step({Action::Select, Action::MenuUp});
        EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_EDITOR) << "Up does not leave";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        s.step({Action::Select, Action::MenuRight});
        EXPECT_EQ(s.ui().preset, preset) << "Right does not step the preset";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    }

    // A value entered: Up held for thirty frames steps nothing and leaves the key repeat alone.
    {
        Probe probe;
        kirpich::setChannelValue(probe.palettes.working, 0, ColorChannel::RED, 100);
        Session s(probe);
        s.toRow(PaletteEditorRow::SHADE_1);
        s.step({Action::Confirm});
        ASSERT_TRUE(s.ui().entering);
        s.game.flow.keyRepeatTimer = 5;
        const int applies          = probe.applies;
        const int saves            = probe.saves;

        s.step({Action::Select, Action::MenuUp});
        for (int frame = 0; frame < 30; ++frame) {
            s.holdFor({Action::Select, Action::MenuUp});
            EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        }
        EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 0, ColorChannel::RED), 100);
        EXPECT_EQ(probe.applies, applies);
        EXPECT_EQ(probe.saves, saves);
        EXPECT_EQ(s.game.flow.keyRepeatTimer, 5);
        EXPECT_TRUE(s.ui().entering);
    }

    // A question up with "yes" chosen: A does not answer it.
    {
        Probe probe;
        probe.palettes = threeSlots();
        Session s(probe);
        s.toRow(PaletteEditorRow::CLEAR);
        s.step({Action::Confirm});
        ASSERT_EQ(s.ui().question, PaletteEditorQuestion::CLEAR);
        s.step({Action::MenuRight});
        ASSERT_TRUE(s.ui().answerYes);

        s.step({Action::Select, Action::Confirm});
        EXPECT_EQ(s.ui().question, PaletteEditorQuestion::CLEAR);
        EXPECT_TRUE(probe.palettes.slots[0].present) << "the slot is untouched";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    }
}

// Releasing Select resumes the editor exactly where it was: an entered value at its place, and a
// question still up. The blink keeps running underneath.
TEST(PalettePreview, ReleasingResumesTheEditorWhereItWas) {
    Probe probe;
    kirpich::setChannelValue(probe.palettes.working, 1, ColorChannel::RED, 100);
    Session s(probe);
    s.toRow(PaletteEditorRow::SHADE_2);
    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    ASSERT_EQ(s.ui().place, DigitPlace::TENS);
    const PaletteEditorState before = s.ui();

    s.game.flow.timer1           = 0;
    s.game.screens.cursorVisible = true;
    s.holdFor({Action::Select});
    EXPECT_EQ(s.game.flow.timer1, kBlinkFrames) << "the blink ran";
    EXPECT_FALSE(s.game.screens.cursorVisible);
    for (int frame = 0; frame < 5; ++frame) {
        s.holdFor({Action::Select});
    }
    s.holdFor({});
    EXPECT_EQ(s.ui(), before);

    s.step({Action::MenuUp});
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 1, ColorChannel::RED), 110)
        << "Up steps by ten, the place it was at";

    Probe asking;
    asking.palettes = threeSlots();
    Session q(asking);
    q.toRow(PaletteEditorRow::CLEAR);
    q.step({Action::Confirm});
    ASSERT_EQ(q.ui().question, PaletteEditorQuestion::CLEAR);
    const PaletteEditorState askingBefore = q.ui();
    q.holdFor({Action::Select});
    q.holdFor({Action::Select});
    q.holdFor({});
    EXPECT_EQ(q.ui(), askingBefore);
}

// Opening the editor and the state's own reset both put the preview down.
TEST(PalettePreview, OpeningAndResetPutThePreviewDown) {
    CustomPalettes p = threeSlots();
    GameContext    game;
    game.paletteEditor.previewing = true;
    kirpich::systems::openPaletteEditor(game, &p);
    EXPECT_FALSE(game.paletteEditor.previewing);

    PaletteEditorState state{.previewing = true};
    state.reset();
    EXPECT_FALSE(state.previewing);
}

// ── The components ────────────────────────────────────────────────────────────────────────────────

// The preview's backdrop is the plain backdrop with the playfield's own left wall down columns 0 and
// 19, row by row, through the custom set's content palette.
TEST(PalettePreview, TheBackdropFlanksTheScreenWithThePlayfieldsBricks) {
    const kirpich::render::Cells cells = kirpich::render::PreviewBackdrop(kAtlas, kCustomRamp);
    const kirpich::render::Cells plain = kirpich::render::Backdrop(kAtlas, kCustomRamp);
    ASSERT_EQ(cells.size(), kirpich::render::kVisibleCells);
    ASSERT_EQ(plain.size(), kirpich::render::kVisibleCells);

    constexpr std::size_t kCols     = kirpich::render::kVisibleCols;
    constexpr std::size_t kWallCol  = kirpich::kPlayingFieldOriginCol - 1;
    for (std::size_t row = 0; row < kirpich::render::kVisibleRows; ++row) {
        const kirpich::render::ResolvedTile brick = kirpich::render::resolveTile(
            kirpich::kTypeAGameplayTilemap[row][kWallCol], kirpich::TileSheet::GAMEPLAY, kAtlas,
            kCustomRamp);
        for (const std::size_t col : {layout::kWallLeftColumn, layout::kWallRightColumn}) {
            const retropp::TileCell& cell = cells[row * kCols + col];
            EXPECT_EQ(cell.atlas, brick.atlas) << "row " << row << " col " << col;
            EXPECT_EQ(cell.tile, brick.cell) << "row " << row << " col " << col;
            EXPECT_EQ(cell.palette, brick.palette) << "row " << row << " col " << col;
            EXPECT_EQ(cell.palette, kAtlas.custom.content)
                << "the content palette, not an object one";
        }
        for (std::size_t col = 0; col < kCols; ++col) {
            if (col == layout::kWallLeftColumn || col == layout::kWallRightColumn) continue;
            EXPECT_TRUE(sameCell(cells[row * kCols + col], plain[row * kCols + col]))
                << "row " << row << " col " << col;
        }
    }

    // The bricks run in a period of three distinct tiles.
    const auto tileAt = [&cells](std::size_t row) { return cells[row * kCols].tile; };
    EXPECT_NE(tileAt(0), tileAt(1));
    EXPECT_NE(tileAt(1), tileAt(2));
    EXPECT_NE(tileAt(0), tileAt(2));
    for (std::size_t row = 3; row < kirpich::render::kVisibleRows; ++row) {
        EXPECT_EQ(tileAt(row), tileAt(row - 3)) << "row " << row;
    }

    // Every game type's playfield has the same wall, so which tilemap the bricks come from is no choice.
    for (std::size_t row = 0; row < kirpich::render::kVisibleRows; ++row) {
        EXPECT_EQ(kirpich::kTypeBGameplayTilemap[row][kWallCol],
                  kirpich::kTypeAGameplayTilemap[row][kWallCol]);
        EXPECT_EQ(kirpich::kTypeCGameplayTilemap[row][kWallCol],
                  kirpich::kTypeAGameplayTilemap[row][kWallCol]);
    }
}

// The three words, top to bottom, each through the font palette it names and centered on the screen.
TEST(PalettePreview, TheThreeWordsThroughTheirThreeInks) {
    const kirpich::render::Sprites preview = kirpich::render::PalettePreview(kAtlas, kCustomRamp);
    const auto&                    custom  = kAtlas.custom;

    struct Word {
        std::string_view   text;
        int                y;
        retropp::PaletteId palette;
    };
    const std::array<Word, 3> words{{
        {"selected", 32, custom.fontSpriteSelected},
        {"unselected", 48, custom.fontSprite},
        {"dimmed", 64, custom.fontSpriteDim},
    }};
    for (std::size_t i = 0; i < words.size(); ++i) {
        const Word& w = words[i];
        EXPECT_EQ(layout::kPreviewWords[i], w.text);
        const int x = layout::previewWordX(w.text.size());

        const kirpich::render::Sprites expected =
            i == 0 ? kirpich::render::SelectedGlyphs(w.text, x, w.y, 8, kAtlas, kCustomRamp)
            : i == 1 ? kirpich::render::Glyphs(w.text, x, w.y, 8, kAtlas, kCustomRamp)
                     : kirpich::render::DimGlyphs(w.text, x, w.y, 8, kAtlas, kCustomRamp);
        expectRun(preview, expected, w.text);

        for (std::size_t c = 0; c < w.text.size(); ++c) {
            const auto glyph = named(preview, glyphKey(x + 8 * static_cast<int>(c), w.y));
            ASSERT_TRUE(glyph) << w.text << " " << c;
            EXPECT_EQ(glyph->palette, w.palette) << w.text;
        }

        const int left  = x;
        const int right = 160 - (x + static_cast<int>(w.text.size()) * 8);
        EXPECT_LE(std::abs(left - right), 1) << w.text << " is centered";
    }
}

// The pieces are the Palette screen's: the same shapes at the same place under another name, through
// the first object palette of the set they are drawn in, on the two shared rows.
TEST(PalettePreview, ThePiecesAreThePaletteScreens) {
    const kirpich::render::Sprites preview = kirpich::render::PalettePreview(kAtlas, kCustomRamp);
    const kirpich::render::Sprites here =
        kirpich::render::PiecePreview("preview-piece", kAtlas, kCustomRamp);
    const kirpich::render::Sprites there =
        kirpich::render::PiecePreview("palette-piece", kAtlas, kCustomRamp);

    kirpich::render::Sprites drawn;
    for (const retropp::Sprite& s : preview) {
        if (s.key.value.starts_with("preview-piece-")) drawn.push_back(s);
    }
    ASSERT_EQ(drawn.size(), here.size());
    ASSERT_FALSE(here.empty());
    for (std::size_t i = 0; i < here.size(); ++i) {
        EXPECT_EQ(drawn[i].key.value, here[i].key.value);
        EXPECT_EQ(drawn[i].x, here[i].x);
        EXPECT_EQ(drawn[i].y, here[i].y);
        EXPECT_EQ(drawn[i].tile, here[i].tile);
        EXPECT_EQ(drawn[i].palette, here[i].palette);
    }

    ASSERT_EQ(here.size(), there.size());
    for (std::size_t i = 0; i < here.size(); ++i) {
        const retropp::Sprite& a = here[i];
        const retropp::Sprite& b = there[i];
        EXPECT_EQ(a.key.value.substr(std::string_view("preview-piece").size()),
                  b.key.value.substr(std::string_view("palette-piece").size()))
            << "the same part under another stem";
        EXPECT_EQ(a.x, b.x);
        EXPECT_EQ(a.y, b.y);
        EXPECT_EQ(a.z, b.z);
        EXPECT_EQ(a.atlas, b.atlas);
        EXPECT_EQ(a.tile, b.tile);
        EXPECT_EQ(a.palette, b.palette);
        EXPECT_EQ(a.flipX, b.flipX);
        EXPECT_EQ(a.palette, kAtlas.custom.sprite0) << a.key.value;
    }

    std::map<std::size_t, int> top;
    for (const retropp::Sprite& s : here) {
        const std::size_t kind = kindOf(s.key.value, "preview-piece");
        const auto        it   = top.find(kind);
        top[kind]              = it == top.end() ? s.y : std::min(it->second, s.y);
    }
    ASSERT_EQ(top.size(), kirpich::kPieceKindCount);
    for (const auto& [kind, y] : top) {
        EXPECT_EQ(y, kind < kirpich::render::kPiecePreviewFirstRowCount
                         ? kirpich::render::kPiecePreviewFirstRowY
                         : kirpich::render::kPiecePreviewSecondRowY)
            << "kind " << kind;
    }
}

// ── The screen ────────────────────────────────────────────────────────────────────────────────────

// While previewing: the brick backdrop, and one sprite layer holding exactly the heading, the
// asterisk while there are unsaved changes, and the preview - no regions, and none of the editor's own
// objects. Not previewing, the two layers are the editor's.
TEST(PalettePreview, TheScreenWhilePreviewing) {
    for (const bool hasChanges : {false, true}) {
        const CustomPalettes p = hasChanges ? unsaved() : saved();
        ASSERT_EQ(kirpich::hasUnsavedChanges(p), hasChanges);

        const auto layers = draw(PaletteEditorState{.preset = 4, .previewing = true}, p, true);
        ASSERT_EQ(layers.size(), 2u);
        EXPECT_EQ(layers[0].key.value, "editor-backdrop");
        expectCellsEqual(cellsOf(layers),
                         kirpich::render::PreviewBackdrop(kAtlas, kCustomRamp));
        EXPECT_EQ(layers[1].key.value, "editor-preview");
        EXPECT_EQ(layers[1].z, layout::kContentZ);
        EXPECT_TRUE(layers[1].regions.empty());

        const auto content = spritesOf(layers);
        const auto heading =
            kirpich::render::Glyphs(layout::kHeading, layout::kHeadingX, layout::kHeadingY, 8,
                                    kAtlas, kCustomRamp);
        const auto preview = kirpich::render::PalettePreview(kAtlas, kCustomRamp);
        expectRun(content, heading, "the heading");
        for (const retropp::Sprite& g : heading) {
            EXPECT_EQ(g.palette, kAtlas.custom.fontSprite);
        }
        expectRun(content, preview, "the preview");

        const auto asterisk = named(content, glyphKey(layout::kAsteriskX, layout::kHeadingY));
        EXPECT_EQ(asterisk.has_value(), hasChanges);
        EXPECT_EQ(content.size(),
                  heading.size() + preview.size() + (hasChanges ? 1u : 0u));

        for (const std::string_view key : kNoPreviewKeys) {
            EXPECT_FALSE(named(content, key)) << key;
        }
        EXPECT_FALSE(named(content, glyphKey(layout::kActionCursorX, layout::kPresetY)))
            << "no cursor";
        EXPECT_FALSE(named(content, glyphKey(layout::kLabelX, layout::kPresetY))) << "no label";
        for (const retropp::Sprite& s : content) {
            EXPECT_NE(s.y, layout::kHintY) << "no hint: " << s.key.value;
        }
    }

    const auto idle = draw(PaletteEditorState{.preset = 4}, saved(), true);
    ASSERT_EQ(idle.size(), 2u);
    expectCellsEqual(cellsOf(idle), kirpich::render::Backdrop(kAtlas, kPlayerRamp));
    EXPECT_EQ(idle[1].key.value, "editor-content");
}

// Every part of the preview draws through the custom set, whether or not the custom palette is the
// one in use.
TEST(PalettePreview, ThePreviewDrawsThroughTheCustomSetWhateverTheSettingsSay) {
    const std::set<retropp::PaletteId> handles = customHandles();
    for (const bool inUse : {false, true}) {
        const Settings s{.shadeRamp = kPlayerRamp, .customPalette = inUse};
        const auto     layers =
            draw(PaletteEditorState{.preset = 4, .previewing = true}, unsaved(), true, s);
        for (const retropp::Sprite& sprite : spritesOf(layers)) {
            EXPECT_TRUE(handles.contains(sprite.palette)) << sprite.key.value;
        }
        for (const retropp::TileCell& cell : cellsOf(layers)) {
            EXPECT_EQ(cell.palette, kAtlas.custom.content);
        }
    }
}

// The preview takes the screen from a question and from an entered value.
TEST(PalettePreview, ThePreviewTakesTheScreenFromAQuestionAndAnEnteredValue) {
    const CustomPalettes p = saved();

    const auto asking = draw(PaletteEditorState{.preset     = 4,
                                                .question   = PaletteEditorQuestion::CLEAR,
                                                .answerYes  = true,
                                                .previewing = true},
                             p, true);
    ASSERT_EQ(asking.size(), 2u);
    EXPECT_EQ(asking[1].key.value, "editor-preview");
    for (const retropp::DrawLayer& layer : asking) {
        EXPECT_NE(layer.key.value, "editor-question");
    }

    const auto entering = draw(PaletteEditorState{.row        = PaletteEditorRow::SHADE_1,
                                                  .entering   = true,
                                                  .place      = DigitPlace::TENS,
                                                  .previewing = true},
                               p, true);
    const auto content  = spritesOf(entering);
    for (const std::string_view key : kNoPreviewKeys) {
        EXPECT_FALSE(named(content, key)) << key;
    }
    std::size_t selected = 0;
    for (const retropp::Sprite& s : content) {
        EXPECT_NE(s.y, layout::kHintY) << "no hint: " << s.key.value;
        if (s.palette == kAtlas.custom.fontSpriteSelected) {
            ++selected;
            EXPECT_EQ(s.y, layout::previewWordY(0)) << "only the word selected: " << s.key.value;
        }
    }
    EXPECT_EQ(selected, layout::kPreviewWords[0].size());
}

// No two objects in a preview frame share a name, and every sprite is on the screen between the two
// walls - swept over every mode the preview can be held from, saved and unsaved.
TEST(PalettePreview, EveryPreviewFrameNamesEachObjectOnceAndStaysBetweenTheWalls) {
    std::vector<PaletteEditorState> states{PaletteEditorState{.preset = 4, .previewing = true}};
    for (const DigitPlace place : {DigitPlace::HUNDREDS, DigitPlace::TENS, DigitPlace::ONES}) {
        for (const PaletteEditorRow row : {PaletteEditorRow::SHADE_1, PaletteEditorRow::SHADE_4}) {
            for (const ColorChannel channel : kChannels) {
                states.push_back(PaletteEditorState{.row        = row,
                                                    .channel    = channel,
                                                    .entering   = true,
                                                    .place      = place,
                                                    .previewing = true});
            }
        }
    }
    for (const PaletteEditorQuestion q : {PaletteEditorQuestion::LOAD_UNSAVED,
                                          PaletteEditorQuestion::SAVE_OVER,
                                          PaletteEditorQuestion::CLEAR}) {
        states.push_back(PaletteEditorState{.preset = 4, .question = q, .previewing = true});
    }

    for (const CustomPalettes& p : {saved(), unsaved()}) {
        for (const PaletteEditorState& ui : states) {
            const auto            layers = draw(ui, p, true);
            std::set<std::string> keys;
            for (const retropp::Sprite& s : spritesOf(layers)) {
                EXPECT_TRUE(keys.insert(s.key.value).second) << "two objects named " << s.key.value;
                EXPECT_GE(s.y, 0) << s.key.value;
                EXPECT_LE(s.y + 8, 144) << s.key.value;
                EXPECT_GE(s.x, layout::kWallLeftX + layout::kBrickSide) << s.key.value;
                EXPECT_LE(s.x + 8, layout::kWallRightX) << s.key.value;
            }
            EXPECT_TRUE(layers[1].regions.empty());
        }
    }
}
