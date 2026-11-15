// The rotation editor - behavioral tests over its logic (src/render/screens/rotation_editor/
// rotation_editor.h), its picture (screen.h beside it and the grid components there), the text input
// screen it raises (src/render/components/text_entry.h) and the colon it adds to the extras sheet
// (src/render/glyph_extras.h).
//
// Device-free. The logic is pure state over the game aggregate and the wirings' seams, and the
// components are pure functions returning primitives, so the atlas here is a plain value with
// distinguishable handles rather than anything uploaded. The screen is the port's own, so every
// asserted value comes from its stated contract.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "render/components/arrow.h"
#include "render/components/backdrop.h"
#include "render/components/glyphs.h"
#include "render/components/level_selector.h"
#include "render/components/palette_preview.h"
#include "render/components/preview_backdrop.h"
#include "render/components/text_entry.h"
#include "render/components/yes_no_question.h"
#include "render/glyph_extras.h"
#include "render/palettes.h"
#include "render/rotation_presets.h"
#include "render/screens/rotation_editor/layout.h"
#include "render/screens/rotation_editor/rotation_editor.h"
#include "render/screens/rotation_editor/screen.h"
#include "render/tile_atlas.h"
#include "retropp/input.h"
#include "state/custom_palettes.h"
#include "state/custom_rotations.h"
#include "state/screen_ui_state.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/input.h"
#include "systems/palette_settings_screen.h"
#include "systems/palette_walk.h"
#include "systems/settings_screen.h"

namespace {

namespace layout = kirpich::render::rotation_editor;

using kirpich::Action;
using kirpich::CustomPalettes;
using kirpich::CustomPaletteSlot;
using kirpich::CustomRotations;
using kirpich::CustomRotationSlot;
using kirpich::GameState;
using kirpich::PaletteRotation;
using kirpich::RotationEditorQuestion;
using kirpich::RotationEditorRow;
using kirpich::RotationEditorState;
using kirpich::RotationName;
using kirpich::RotationProgression;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::SquareSfxId;
using kirpich::render::kRotationRamp;
using kirpich::render::kShadeRampCount;
using kirpich::render::RampPalettes;
using kirpich::render::ShadeRamp;
using kirpich::render::TileAtlas;
using kirpich::systems::CustomPaletteWiring;
using kirpich::systems::GameContext;
using kirpich::systems::PaletteChoice;
using kirpich::systems::RotationChoice;
using kirpich::systems::RotationWiring;
using kirpich::systems::SettingsWiring;

constexpr std::uint8_t kPlayerRamp  = 7;
constexpr std::uint8_t kBlinkFrames = 16;
constexpr std::uint8_t kLastRamp    = static_cast<std::uint8_t>(kShadeRampCount - 1);

TileAtlas makeAtlas() {
    TileAtlas atlas{
        .font             = static_cast<retropp::AtlasId>(11),
        .copyrightTitle   = static_cast<retropp::AtlasId>(22),
        .gameplay         = static_cast<retropp::AtlasId>(33),
        .multiplayerBuran = static_cast<retropp::AtlasId>(44),
        .glyphExtras      = static_cast<retropp::AtlasId>(55),
    };
    const auto fill = [](RampPalettes& set, std::size_t ramp) {
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
    fill(atlas.custom, kirpich::render::kCustomRamp);
    fill(atlas.rotation, kRotationRamp);
    return atlas;
}

const TileAtlas kAtlas = makeAtlas();

// Ten distinct palettes from one base, so rotations - and the levels within one - can be told apart.
RotationProgression distinct(std::uint8_t base) {
    RotationProgression r;
    for (std::size_t j = 0; j < kirpich::kRotationLevelCount; ++j) {
        const auto v = static_cast<std::uint8_t>(base + 3 * j);
        r.levels[j]  = ShadeRamp{.darkest  = {.r = v, .g = 1, .b = 2},
                                 .dark     = {.r = v, .g = 60, .b = 61},
                                 .light    = {.r = v, .g = 120, .b = 121},
                                 .lightest = {.r = v, .g = 200, .b = 201}};
    }
    return r;
}

// Four distinct colors from one base, unlike any ramp's, so palettes can be told apart.
ShadeRamp rampFrom(std::uint8_t base) {
    const auto c = [base](std::uint8_t step) {
        return retropp::Rgba8{.r = static_cast<std::uint8_t>(base + step),
                              .g = static_cast<std::uint8_t>(base + step + 1),
                              .b = static_cast<std::uint8_t>(base + step + 2)};
    };
    return ShadeRamp{.darkest = c(0), .dark = c(10), .light = c(20), .lightest = c(30)};
}

RotationName nameOf(std::string_view text) {
    RotationName name = kirpich::blankRotationName();
    std::copy(text.begin(), text.end(), name.begin());
    return name;
}

// Rotation slots 1, 4 and 7 saved, each ten distinct palettes; the working rotation the fresh one.
CustomRotations threeRotations() {
    CustomRotations r;
    r.slots[0] = CustomRotationSlot{.present = true, .progression = distinct(10)};
    r.slots[3] = CustomRotationSlot{.present = true, .progression = distinct(40)};
    r.slots[6] = CustomRotationSlot{.present = true, .progression = distinct(70)};
    return r;
}

// Palette slots 1, 4 and 7 saved.
CustomPalettes threePalettes() {
    CustomPalettes p;
    p.slots[0] = CustomPaletteSlot{.present = true, .shades = rampFrom(10)};
    p.slots[3] = CustomPaletteSlot{.present = true, .shades = rampFrom(40)};
    p.slots[6] = CustomPaletteSlot{.present = true, .shades = rampFrom(70)};
    return p;
}

// A ramp's colors, as the walk copies them into a level.
ShadeRamp rampColors(std::uint8_t ramp) {
    return kirpich::systems::colorsOf(PaletteChoice{.custom = false, .value = ramp}, CustomPalettes{});
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
    Settings        settings{.shadeRamp = kPlayerRamp};
    CustomRotations rotations = threeRotations();
    CustomPalettes  palettes  = threePalettes();
    int             settingsSaves = 0;
    int             rotationSaves = 0;

    SettingsWiring wiring() {
        return SettingsWiring{
            .settings = &settings,
            .save     = [this](const Settings&) { ++settingsSaves; },
        };
    }

    RotationWiring rotation() {
        return RotationWiring{
            .rotations = &rotations,
            .save      = [this](const CustomRotations&) { ++rotationSaves; },
        };
    }
};

// The editor opened the way a player opens it: the settings screen from the title, the Palette screen
// from its row, Up to the rotation screen, Up to its editor item, and A. With no rotations behind the
// screen the item opens nothing, so the editor is entered directly.
GameContext openEditor(const SettingsWiring& wiring, const RotationWiring& rotations) {
    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, wiring);
    game.screens.settingsRow = SettingsRow::PALETTE;
    press(game, {Action::Confirm});
    kirpich::systems::settingsScreen(game, wiring);
    press(game, {Action::MenuUp});
    kirpich::systems::paletteSettingsScreen(game, wiring, CustomPaletteWiring{});
    press(game, {Action::MenuUp});
    kirpich::systems::rotationScreen(game, wiring, rotations);
    press(game, {Action::Confirm});
    kirpich::systems::rotationScreen(game, wiring, rotations);
    if (rotations.rotations == nullptr) {
        kirpich::systems::openRotationEditor(game, nullptr);
    }
    return game;
}

struct Session {
    Probe&                probe;
    SettingsWiring        wiring;
    RotationWiring        rotations;
    const CustomPalettes* palettes;
    GameContext           game;

    explicit Session(Probe& p, bool wired = true, bool withPalettes = true)
        : probe(p),
          wiring(p.wiring()),
          rotations(wired ? p.rotation() : RotationWiring{}),
          palettes(withPalettes ? &p.palettes : nullptr),
          game(openEditor(wiring, rotations)) {}

    void step(std::initializer_list<Action> as) {
        press(game, as);
        kirpich::systems::rotationEditor(game, wiring, rotations, palettes);
    }
    void holdFor(std::initializer_list<Action> as) {
        hold(game, as);
        kirpich::systems::rotationEditor(game, wiring, rotations, palettes);
    }
    // Select held, and `as` pressed on this frame.
    void previewStep(std::initializer_list<Action> as) {
        game.joypad.pressed = actionSet(as);
        game.joypad.held    = actionSet(as);
        game.joypad.held.set(retropp::actionId(Action::Select), true);
        game.audioCues = kirpich::systems::AudioCues{};
        kirpich::systems::rotationEditor(game, wiring, rotations, palettes);
    }
    RotationEditorState& ui() { return game.rotationEditor; }
    SquareSfxId          sound() const { return game.audioCues.square; }

    // The cursor onto `row`, walking up or down to it.
    void toRow(RotationEditorRow row) {
        while (ui().row != row) step({ui().row > row ? Action::MenuUp : Action::MenuDown});
    }
    // The cursor onto level `level`'s cell.
    void toLevel(std::uint8_t level) {
        toRow(RotationEditorRow::CLEAR);
        step({Action::MenuDown});
        if (ui().level >= 5 && level < 5) step({Action::MenuLeft});
        if (ui().level < 5 && level >= 5) step({Action::MenuRight});
        while (ui().level < level) step({Action::MenuDown});
    }
    // The preset selector onto `target`, from where it stands.
    void toPreset(RotationChoice target) {
        toRow(RotationEditorRow::PRESET);
        const auto rank = [](RotationChoice c) {
            return c.mode == PaletteRotation::CUSTOM ? 3 + c.slot : static_cast<int>(c.mode);
        };
        while (rank(ui().preset) < rank(target)) step({Action::MenuRight});
        while (rank(ui().preset) > rank(target)) step({Action::MenuLeft});
    }
};

RotationChoice slot(std::uint8_t n) { return RotationChoice{.mode = PaletteRotation::CUSTOM, .slot = n}; }
RotationChoice shipped(PaletteRotation m) { return RotationChoice{.mode = m}; }

kirpich::render::Layers draw(const RotationEditorState& ui, const CustomRotations& rotations,
                             const CustomPalettes& palettes, bool blinkOn = false,
                             Settings s = Settings{.shadeRamp = kPlayerRamp}) {
    return kirpich::render::RotationEditorScreen(ui, s, rotations, palettes, blinkOn, kAtlas);
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

// Every sprite `expected` holds is in `content`, by name, with the same tile, sheet, palette and place.
void expectSprites(std::span<const retropp::Sprite> content, const kirpich::render::Sprites& expected,
                   std::string_view what) {
    for (const retropp::Sprite& want : expected) {
        const auto got = named(content, want.key.value);
        ASSERT_TRUE(got) << what << " at " << want.key.value;
        EXPECT_EQ(got->tile, want.tile) << what << " " << want.key.value;
        EXPECT_EQ(got->atlas, want.atlas) << what << " " << want.key.value;
        EXPECT_EQ(got->palette, want.palette) << what << " " << want.key.value;
        EXPECT_EQ(got->x, want.x) << what << " " << want.key.value;
        EXPECT_EQ(got->y, want.y) << what << " " << want.key.value;
        EXPECT_EQ(got->rotation, want.rotation) << what << " " << want.key.value;
        EXPECT_EQ(got->flipX, want.flipX) << what << " " << want.key.value;
    }
}

const retropp::Region* region(const kirpich::render::Layers& layers, std::string_view key) {
    for (const retropp::Region& r : layers[1].regions) {
        if (r.key.value == key) return &r;
    }
    return nullptr;
}

std::set<retropp::PaletteId> handlesOf(const RampPalettes& set) {
    return {set.font,          set.content,   set.fontDim,
            set.fontSprite,    set.sprite0,   set.sprite1,
            set.fontSpriteDim, set.spriteDim, set.fontSpriteSelected};
}

[[nodiscard]] int rowY(RotationEditorRow row) {
    switch (row) {
        case RotationEditorRow::LOAD:
            return layout::kLoadY;
        case RotationEditorRow::SAVE:
            return layout::kSaveY;
        case RotationEditorRow::CLEAR:
            return layout::kClearY;
        default:
            return layout::kPresetY;
    }
}

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

// (1) Opening resets the state from a dirty one, seeds the preset on the slot the working rotation is
// saved in - slot 1 when it is saved in none and when there are no rotations - shows the cursor, arms
// the blink and enters the screen.
TEST(RotationEditor, OpeningResetsTheStateAndSeedsThePreset) {
    CustomRotations r = threeRotations();
    r.working         = r.slots[3].progression;

    GameContext game;
    game.flow.timer1           = 3;
    game.screens.cursorVisible = false;
    game.rotationEditor        = RotationEditorState{.row       = RotationEditorRow::LEVELS,
                                                     .level     = 8,
                                                     .preset    = slot(9),
                                                     .entering  = true,
                                                     .naming    = true,
                                                     .namePlace = 4,
                                                     .question  = RotationEditorQuestion::CLEAR,
                                                     .answerYes = true,
                                                     .previewing = true};

    kirpich::systems::openRotationEditor(game, &r);
    RotationEditorState want;
    want.preset = slot(4);
    EXPECT_EQ(game.rotationEditor, want);
    EXPECT_EQ(game.flow.gameState, GameState::ROTATION_EDITOR);
    EXPECT_TRUE(game.screens.cursorVisible);
    EXPECT_EQ(game.flow.timer1, kBlinkFrames);

    r.working = distinct(99);
    kirpich::systems::openRotationEditor(game, &r);
    EXPECT_EQ(game.rotationEditor.preset, slot(1)) << "unsaved opens on slot 1";

    kirpich::systems::openRotationEditor(game, nullptr);
    EXPECT_EQ(game.rotationEditor.preset, slot(1)) << "no rotations opens on slot 1";
}

// (2) The cursor walks the four rows and into the grid: Up on the preset is an end stop that cues
// nothing; Down from clear lands on the top cell of the column the cursor was last in; Up from a
// column's top cell returns to clear; Down on a column's bottom cell is an end stop.
TEST(RotationEditor, TheCursorWalksTheRowsAndIntoTheGrid) {
    Probe   probe;
    Session s(probe);
    ASSERT_EQ(s.game.flow.gameState, GameState::ROTATION_EDITOR);
    ASSERT_EQ(s.ui().row, RotationEditorRow::PRESET);

    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().row, RotationEditorRow::PRESET);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    for (const RotationEditorRow row :
         {RotationEditorRow::LOAD, RotationEditorRow::SAVE, RotationEditorRow::CLEAR}) {
        s.step({Action::MenuDown});
        EXPECT_EQ(s.ui().row, row);
        EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    }

    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().row, RotationEditorRow::LEVELS);
    EXPECT_EQ(s.ui().level, 0);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);

    for (int i = 0; i < 4; ++i) s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().level, 4);
    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().level, 4) << "the column's bottom cell is an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    // Into the right column, down to level 8, then back up and out.
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().level, 9);
    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().level, 8);
    for (int i = 0; i < 3; ++i) s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().level, 5);
    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().row, RotationEditorRow::CLEAR);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);

    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().row, RotationEditorRow::LEVELS);
    EXPECT_EQ(s.ui().level, 5) << "the top of the column the cursor was last in";
}

// (3) The grid is two columns of five: Down walks 0 to 4 and 5 to 9, Right from `i` lands on `i + 5`
// and Left goes back, Left in the left column and Right in the right one are end stops. The cells
// stand where the layout says, column by column.
TEST(RotationEditor, TheGridIsTwoColumnsOfFive) {
    Probe   probe;
    Session s(probe);
    s.toLevel(0);
    for (std::uint8_t i = 0; i < 5; ++i) {
        EXPECT_EQ(s.ui().level, i);
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui().level, i + 5);
        EXPECT_EQ(s.sound(), SquareSfxId::TINK);
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui().level, i + 5) << "the right column's Right is an end stop";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        s.step({Action::MenuLeft});
        EXPECT_EQ(s.ui().level, i);
        s.step({Action::MenuLeft});
        EXPECT_EQ(s.ui().level, i) << "the left column's Left is an end stop";
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        s.step({Action::MenuDown});
    }

    // Entering the grid from the rows above lands on the top of level 6's column, which is 5.
    s.ui().row   = RotationEditorRow::CLEAR;
    s.ui().level = 6;
    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().level, 5);

    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(layout::cellX(i), i < 5 ? 0 : 80) << "level " << i;
        EXPECT_EQ(layout::cellY(i), 68 + 15 * static_cast<int>(i % 5)) << "level " << i;
    }
}

// (4) B returns to the rotation screen with its cursor on the editor item, cueing the screen change,
// from any row, the rotations and the settings untouched.
TEST(RotationEditor, BReturnsToTheRotationScreenOnItsEditorItem) {
    for (const RotationEditorRow row : {RotationEditorRow::PRESET, RotationEditorRow::CLEAR,
                                        RotationEditorRow::LEVELS}) {
        Probe   probe;
        Session s(probe);
        s.toRow(row);
        const Settings        before    = probe.settings;
        const CustomRotations rotations = probe.rotations;
        s.step({Action::Back});
        EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_ROTATION);
        EXPECT_EQ(s.game.rotationScreen.row, kirpich::RotationScreenRow::EDITOR);
        EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
        EXPECT_EQ(probe.settings, before);
        EXPECT_EQ(probe.rotations, rotations);
        EXPECT_EQ(probe.rotationSaves, 0);
    }
}

// (5) The preset selector steps every stop both ways - hotter, colder, rainbow, then slots 1 to 99 -
// with an end stop at each end, a cue per step, and nothing with no rotations.
TEST(RotationEditor, ThePresetSelectorStepsEveryStopBothWays) {
    Probe   probe;
    Session s(probe);
    ASSERT_EQ(s.ui().preset, slot(1));

    std::vector<RotationChoice> order{shipped(PaletteRotation::HOTTER),
                                      shipped(PaletteRotation::COLDER),
                                      shipped(PaletteRotation::RAINBOW)};
    for (std::uint8_t n = 1; n <= 99; ++n) order.push_back(slot(n));

    for (int i = 0; i < 3; ++i) s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().preset, order.front());
    s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().preset, order.front()) << "hotter's Left is an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    for (std::size_t i = 1; i < order.size(); ++i) {
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui().preset, order[i]) << "step " << i;
        EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    }
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().preset, order.back()) << "slot 99's Right is an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    for (std::size_t i = order.size() - 1; i-- > 0;) {
        s.step({Action::MenuLeft});
        EXPECT_EQ(s.ui().preset, order[i]) << "step back " << i;
    }

    Probe   none;
    Session n(none, /*wired=*/false);
    n.step({Action::MenuLeft});
    EXPECT_EQ(n.ui().preset, slot(1));
    EXPECT_EQ(n.sound(), SquareSfxId::NONE);
    n.step({Action::MenuRight});
    EXPECT_EQ(n.ui().preset, slot(1));
}

// (6) The preset's arrows stand exactly where a step exists, at every kind of stop.
TEST(RotationEditor, ThePresetsArrowsAgreeWithItsSteps) {
    const CustomRotations r = threeRotations();
    const CustomPalettes  p = threePalettes();
    for (const RotationChoice preset :
         {shipped(PaletteRotation::HOTTER), shipped(PaletteRotation::COLDER),
          shipped(PaletteRotation::RAINBOW), slot(1), slot(2), slot(50), slot(99)}) {
        const auto content = spritesOf(draw(RotationEditorState{.preset = preset}, r, p));
        const bool left    = kirpich::systems::rotationEditorPresetCanStep(preset, &r, -1);
        const bool right   = kirpich::systems::rotationEditorPresetCanStep(preset, &r, +1);
        EXPECT_EQ(named(content, "rotation-editor-preset-left").has_value(), left);
        EXPECT_EQ(named(content, "rotation-editor-preset-right").has_value(), right);
        if (left) {
            expectSprites(content,
                          kirpich::render::Arrow("rotation-editor-preset-left",
                                                 layout::kPresetLeftArrowX, layout::kPresetY,
                                                 kirpich::render::ArrowDirection::LEFT,
                                                 layout::kArrowZ, kAtlas, kPlayerRamp),
                          "left");
        }
        if (right) {
            expectSprites(content,
                          kirpich::render::Arrow("rotation-editor-preset-right",
                                                 layout::kPresetRightArrowX, layout::kPresetY,
                                                 kirpich::render::ArrowDirection::RIGHT,
                                                 layout::kArrowZ, kAtlas, kPlayerRamp),
                          "right");
        }
    }
    EXPECT_FALSE(kirpich::systems::rotationEditorPresetCanStep(shipped(PaletteRotation::HOTTER), &r, -1));
    EXPECT_TRUE(kirpich::systems::rotationEditorPresetCanStep(shipped(PaletteRotation::RAINBOW), &r, +1));
    EXPECT_TRUE(kirpich::systems::rotationEditorPresetCanStep(slot(1), &r, -1));
    EXPECT_FALSE(kirpich::systems::rotationEditorPresetCanStep(slot(99), &r, +1));
    EXPECT_FALSE(kirpich::systems::rotationEditorPresetCanStep(slot(50), nullptr, +1));
}

// (7) What each action can do: a shipped rotation loads while it differs from the working rotation
// and can be neither saved nor cleared; an empty slot can only be saved into; a present slot holding
// the working rotation can be saved again and cleared; one holding another rotation can do all three.
TEST(RotationEditor, WhatEachActionCanDo) {
    using kirpich::systems::rotationEditorActionCanAct;
    CustomRotations r = threeRotations();
    r.working         = r.slots[3].progression;

    struct Row {
        RotationChoice preset;
        bool           load, save, clear;
    };
    for (const Row& row : {Row{shipped(PaletteRotation::HOTTER), true, false, false},
                           Row{slot(2), false, true, false},
                           Row{slot(4), false, true, true},
                           Row{slot(1), true, true, true}}) {
        EXPECT_EQ(rotationEditorActionCanAct(RotationEditorRow::LOAD, row.preset, &r), row.load);
        EXPECT_EQ(rotationEditorActionCanAct(RotationEditorRow::SAVE, row.preset, &r), row.save);
        EXPECT_EQ(rotationEditorActionCanAct(RotationEditorRow::CLEAR, row.preset, &r), row.clear);
        EXPECT_FALSE(rotationEditorActionCanAct(RotationEditorRow::PRESET, row.preset, &r));
        EXPECT_FALSE(rotationEditorActionCanAct(RotationEditorRow::LEVELS, row.preset, &r));
        for (const RotationEditorRow a :
             {RotationEditorRow::LOAD, RotationEditorRow::SAVE, RotationEditorRow::CLEAR}) {
            EXPECT_FALSE(rotationEditorActionCanAct(a, row.preset, nullptr));
        }
    }

    r.working.levels = kirpich::render::kHotterRotation;
    EXPECT_FALSE(rotationEditorActionCanAct(RotationEditorRow::LOAD,
                                            shipped(PaletteRotation::HOTTER), &r))
        << "a shipped rotation already in the working one has nothing to load";
}

// (8) Loading a shipped rotation seeds the working one: its ten palettes, the document written once,
// no question asked of a fresh working rotation, and the rotation mode left as it is.
TEST(RotationEditor, LoadingAShippedRotationSeedsTheWorkingOne) {
    Probe   probe;
    Session s(probe);
    s.toPreset(shipped(PaletteRotation::HOTTER));
    s.toRow(RotationEditorRow::LOAD);
    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, RotationEditorQuestion::NONE);
    EXPECT_EQ(probe.rotations.working.levels, kirpich::render::kHotterRotation);
    EXPECT_EQ(probe.rotationSaves, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    EXPECT_EQ(probe.settings.paletteRotation, PaletteRotation::OFF);
    EXPECT_EQ(probe.settingsSaves, 0);
}

// (9) Load asks first while the working rotation has unsaved changes: "no" and B change nothing, and
// "yes" loads.
TEST(RotationEditor, LoadAsksFirstWhileTheWorkingRotationIsUnsaved) {
    Probe probe;
    probe.rotations.working = distinct(99);
    Session s(probe);
    s.toRow(RotationEditorRow::LOAD);

    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, RotationEditorQuestion::LOAD_UNSAVED);
    EXPECT_EQ(probe.rotations.working, distinct(99));
    s.step({Action::Confirm});  // on "no"
    EXPECT_EQ(s.ui().question, RotationEditorQuestion::NONE);
    EXPECT_EQ(probe.rotations.working, distinct(99));

    s.step({Action::Confirm});
    s.step({Action::Back});
    EXPECT_EQ(s.ui().question, RotationEditorQuestion::NONE);
    EXPECT_EQ(probe.rotations.working, distinct(99));
    EXPECT_EQ(probe.rotationSaves, 0);

    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    s.step({Action::Confirm});
    EXPECT_EQ(probe.rotations.working, probe.rotations.slots[0].progression);
    EXPECT_EQ(probe.rotationSaves, 1);
}

// (10) Save asks only when it would replace a different rotation: over a present slot holding another
// rotation it asks first; into an empty slot, and onto the slot that already holds the working
// rotation, it raises the name screen at once.
TEST(RotationEditor, SaveAsksOnlyWhenItWouldReplaceADifferentRotation) {
    {
        Probe probe;
        probe.rotations.working = distinct(99);
        Session s(probe);
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        EXPECT_EQ(s.ui().question, RotationEditorQuestion::SAVE_OVER);
        EXPECT_FALSE(s.ui().naming);
    }
    {
        Probe probe;
        probe.rotations.working = distinct(99);
        Session s(probe);
        s.toPreset(slot(2));
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        EXPECT_EQ(s.ui().question, RotationEditorQuestion::NONE);
        EXPECT_TRUE(s.ui().naming);
    }
    {
        Probe probe;
        probe.rotations.working = probe.rotations.slots[3].progression;
        Session s(probe);
        ASSERT_EQ(s.ui().preset, slot(4));
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        EXPECT_EQ(s.ui().question, RotationEditorQuestion::NONE);
        EXPECT_TRUE(s.ui().naming);
    }
}

// (11) Clear asks, then empties the slot; clearing the only slot that holds the rotation in use turns
// the rotation off through the settings; with a second slot holding it, or the mode not CUSTOM, the
// settings are untouched.
TEST(RotationEditor, ClearAsksThenEmptiesTheSlot) {
    struct Case {
        PaletteRotation mode;
        bool            secondCopy;
        PaletteRotation after;
    };
    for (const Case c : {Case{PaletteRotation::CUSTOM, false, PaletteRotation::OFF},
                         Case{PaletteRotation::CUSTOM, true, PaletteRotation::CUSTOM},
                         Case{PaletteRotation::HOTTER, false, PaletteRotation::HOTTER}}) {
        Probe probe;
        probe.settings.paletteRotation = c.mode;
        probe.rotations.working        = probe.rotations.slots[3].progression;
        if (c.secondCopy) probe.rotations.slots[6].progression = probe.rotations.working;
        Session s(probe);
        ASSERT_EQ(s.ui().preset, slot(4));
        s.toRow(RotationEditorRow::CLEAR);

        s.step({Action::Confirm});
        ASSERT_EQ(s.ui().question, RotationEditorQuestion::CLEAR);
        EXPECT_TRUE(probe.rotations.slots[3].present) << "nothing before the answer";
        s.step({Action::MenuRight});
        s.step({Action::Confirm});

        EXPECT_EQ(probe.rotations.slots[3], CustomRotationSlot{});
        EXPECT_EQ(probe.rotationSaves, 1);
        EXPECT_EQ(probe.settings.paletteRotation, c.after) << "mode " << int(c.mode);
        EXPECT_EQ(probe.settingsSaves, c.after != c.mode ? 1 : 0);
        EXPECT_EQ(probe.rotations.working, distinct(40)) << "the working rotation is left as it is";
    }
}

// (12) A question opens on "no", picks with Left and Right, closes with B, re-arms the cursor, cues as
// the screen does, and names its slot as two digits.
TEST(RotationEditor, AQuestionOpensOnNoAndPicks) {
    Probe probe;
    probe.rotations.working = probe.rotations.slots[3].progression;
    Session s(probe);
    s.toRow(RotationEditorRow::CLEAR);
    s.game.screens.cursorVisible = false;
    s.game.flow.timer1           = 2;

    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, RotationEditorQuestion::CLEAR);
    EXPECT_FALSE(s.ui().answerYes);
    EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
    EXPECT_TRUE(s.game.screens.cursorVisible);
    EXPECT_EQ(s.game.flow.timer1, kBlinkFrames);

    s.step({Action::MenuLeft});
    EXPECT_FALSE(s.ui().answerYes);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    s.step({Action::MenuRight});
    EXPECT_TRUE(s.ui().answerYes);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);

    const auto layers = draw(s.ui(), probe.rotations, probe.palettes, true);
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[1].key.value, "rotation-editor-question");
    const retropp::DrawLayer want = kirpich::render::YesNoQuestion(
        "rotation-editor-question-want", "clear preset", "empty", "preset 04", true, true, kAtlas,
        kPlayerRamp);
    const auto wantSprites = std::get<retropp::SpriteContent>(want.content).sprites;
    expectSprites(spritesOf(layers),
                  kirpich::render::Sprites(wantSprites.begin(), wantSprites.end()), "the question");

    s.step({Action::Back});
    EXPECT_EQ(s.ui().question, RotationEditorQuestion::NONE);
    EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
    EXPECT_TRUE(probe.rotations.slots[3].present);
    EXPECT_EQ(probe.rotationSaves, 0);
}

// (13) The action labels dim exactly where the action cannot act, at every kind of stop.
TEST(RotationEditor, TheActionLabelsDimWhereTheyCannotAct) {
    CustomRotations r = threeRotations();
    r.working         = r.slots[3].progression;
    const CustomPalettes p = threePalettes();
    for (const RotationChoice preset :
         {shipped(PaletteRotation::HOTTER), slot(2), slot(4), slot(1)}) {
        const auto content = spritesOf(draw(RotationEditorState{.preset = preset}, r, p));
        for (const auto& [label, action] :
             {std::pair{std::string_view{"load preset"}, RotationEditorRow::LOAD},
              std::pair{std::string_view{"save preset"}, RotationEditorRow::SAVE},
              std::pair{std::string_view{"clear preset"}, RotationEditorRow::CLEAR}}) {
            const bool acts = kirpich::systems::rotationEditorActionCanAct(action, preset, &r);
            expectSprites(content,
                          acts ? kirpich::render::Glyphs(label, layout::kLabelX, rowY(action),
                                                         layout::kGlyph, kAtlas, kPlayerRamp)
                               : kirpich::render::DimGlyphs(label, layout::kLabelX, rowY(action),
                                                            layout::kGlyph, kAtlas, kPlayerRamp),
                          label);
        }
    }
}

// (14) A or Start on a cell enters its number: the stop its colors stand on, the colors it wore, and
// the key repeat zeroed. On the rows above, and with no rotations, they enter nothing.
TEST(RotationEditor, EnteringACell) {
    for (const Action a : {Action::Confirm, Action::Start}) {
        Probe probe;
        probe.rotations.working.levels[3] = rampColors(12);
        Session s(probe);
        s.toLevel(3);
        s.game.flow.keyRepeatTimer = 5;
        s.step({a});
        EXPECT_TRUE(s.ui().entering);
        EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = 12}));
        EXPECT_EQ(s.ui().colorsOnEntry, rampColors(12));
        EXPECT_EQ(s.game.flow.keyRepeatTimer, 0);
        EXPECT_EQ(probe.rotationSaves, 0);
    }

    Probe probe;
    Session s(probe);
    s.toPreset(slot(2));
    s.toRow(RotationEditorRow::CLEAR);  // nothing to clear in an empty slot
    s.step({Action::Confirm});
    EXPECT_FALSE(s.ui().entering);

    Probe   none;
    Session n(none, /*wired=*/false);
    n.toLevel(0);
    n.step({Action::Confirm});
    EXPECT_FALSE(n.ui().entering);
}

// (15) A step copies the stop's colors into the level and writes the document: every other level
// untouched, the cue a tink. Left on the first ramp is an end stop that writes and cues nothing.
TEST(RotationEditor, AStepCopiesTheStopsColorsIntoTheLevel) {
    Probe probe;
    probe.rotations.working           = distinct(150);
    probe.rotations.working.levels[2] = rampColors(5);
    probe.rotations.working.levels[0] = rampColors(0);
    const RotationProgression before  = probe.rotations.working;
    Session s(probe);
    s.toLevel(2);
    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    EXPECT_EQ(probe.rotations.working.levels[2], rampColors(6));
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = 6}));
    for (std::size_t j = 0; j < 10; ++j) {
        if (j != 2) EXPECT_EQ(probe.rotations.working.levels[j], before.levels[j]) << "level " << j;
    }
    EXPECT_EQ(probe.rotationSaves, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::Confirm});

    s.step({Action::MenuUp});
    s.step({Action::MenuUp});
    ASSERT_EQ(s.ui().level, 0);
    s.step({Action::Confirm});
    s.step({Action::MenuLeft});
    EXPECT_EQ(probe.rotations.working.levels[0], rampColors(0));
    EXPECT_EQ(probe.rotationSaves, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
}

// (16) The walk runs on from the last ramp through the saved palettes and ends: 80, C01, C04, C07, an
// end stop, and back to 80.
TEST(RotationEditor, TheWalkRunsOnThroughThePaletteSlotsAndEnds) {
    Probe probe;
    probe.rotations.working.levels[0] = rampColors(kLastRamp);
    Session s(probe);
    s.toLevel(0);
    s.step({Action::Confirm});

    for (const std::uint8_t n : {1, 4, 7}) {
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = true, .value = n}));
        EXPECT_EQ(probe.rotations.working.levels[0],
                  kirpich::systems::colorsOf(PaletteChoice{.custom = true, .value = n},
                                             probe.palettes));
    }
    const int saves = probe.rotationSaves;
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = true, .value = 7})) << "an end stop";
    EXPECT_EQ(probe.rotationSaves, saves);

    for (int i = 0; i < 3; ++i) s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = kLastRamp}));
    EXPECT_EQ(probe.rotations.working.levels[0], rampColors(kLastRamp));
}

// (17) A palette slot holding a ramp's colors does not trap the walk: the step moves from where the
// entered number stands, not from the stop the level's colors read back as.
TEST(RotationEditor, ASlotHoldingARampsColorsDoesNotTrapTheWalk) {
    Probe probe;
    probe.palettes.slots[0].shades    = rampColors(5);
    probe.rotations.working.levels[0] = rampColors(kLastRamp);
    Session s(probe);
    s.toLevel(0);
    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = true, .value = 1}));
    EXPECT_EQ(probe.rotations.working.levels[0], rampColors(5));
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = true, .value = 4}));
}

// (18) Left and Right repeat while held, on the game's own key repeat: a press steps at once, a hold
// steps again after the initial delay and then every repeat interval.
TEST(RotationEditor, LeftAndRightRepeatWhileHeld) {
    Probe probe;
    probe.rotations.working.levels[0] = rampColors(10);
    Session s(probe);
    s.toLevel(0);
    s.step({Action::Confirm});

    std::vector<int> stepped;
    for (int frame = 1; frame <= 60; ++frame) {
        const int before = probe.rotationSaves;
        if (frame == 1) {
            s.step({Action::MenuRight});
        } else {
            s.holdFor({Action::MenuRight});
        }
        if (probe.rotationSaves != before) stepped.push_back(frame);
    }
    const int delay = kirpich::systems::kKeyRepeatInitialDelay;
    const int rate  = kirpich::systems::kKeyRepeatRate;
    std::vector<int> want{1};
    for (int f = 1 + delay; f <= 60; f += rate) want.push_back(f);
    EXPECT_EQ(stepped, want);
    EXPECT_EQ(stepped, (std::vector<int>{1, 24, 33, 42, 51, 60}));
}

// (19) A keeps the number and cues; B puts back the palette the level wore when it was entered, writes
// the document once more, cues nothing and stays on the screen - and with nothing changed writes
// nothing.
// (18a) Up and Down step an entered number ten stops along the walk, on the key repeat, and clamp: a
// ten-step runs as far as the walk goes and stops at its end, and is an end stop only when it cannot
// move at all. Its arrows are case (21a)'s.
TEST(RotationEditor, UpAndDownStepTheEnteredNumberByTen) {
    Probe probe;
    probe.rotations.working.levels[0] = rampColors(19);  // reads 20
    Session s(probe);
    s.toLevel(0);
    s.step({Action::Confirm});

    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = 29}));
    EXPECT_EQ(probe.rotations.working.levels[0], rampColors(29));
    EXPECT_EQ(probe.rotationSaves, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = 19}));
    EXPECT_EQ(probe.rotationSaves, 2);

    // Held, it repeats on the key repeat, as Left and Right do.
    std::vector<int> stepped;
    for (int frame = 1; frame <= 33; ++frame) {
        const int before = probe.rotationSaves;
        if (frame == 1) {
            s.step({Action::MenuUp});
        } else {
            s.holdFor({Action::MenuUp});
        }
        if (probe.rotationSaves != before) stepped.push_back(frame);
    }
    EXPECT_EQ(stepped, (std::vector<int>{1, 24, 33}));
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = 49}));

    // Near the end of the ramps, a ten-step runs on through the saved palettes and stops at the last.
    s.ui().entered = PaletteChoice{.custom = false, .value = 75};
    s.holdFor({});
    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = true, .value = 7}))
        << "76 to 80, then C01, C04 and C07, and no further";
    const int saves = probe.rotationSaves;
    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = true, .value = 7}));
    EXPECT_EQ(probe.rotationSaves, saves) << "at the end it cannot move, so nothing is written";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    // Near the start, a ten-step back stops at the first ramp.
    s.ui().entered = PaletteChoice{.custom = false, .value = 3};
    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = 0}));
    EXPECT_EQ(probe.rotations.working.levels[0], rampColors(0));
    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = 0}));
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
}

TEST(RotationEditor, AKeepsTheNumberAndBPutsItBack) {
    Probe probe;
    probe.rotations.working.levels[1] = rampColors(10);
    Session s(probe);
    s.toLevel(1);

    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    s.step({Action::MenuRight});
    s.step({Action::Confirm});
    EXPECT_FALSE(s.ui().entering);
    EXPECT_EQ(probe.rotations.working.levels[1], rampColors(12));
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);

    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    s.step({Action::MenuRight});
    const int saves = probe.rotationSaves;
    s.step({Action::Back});
    EXPECT_FALSE(s.ui().entering);
    EXPECT_EQ(probe.rotations.working.levels[1], rampColors(12));
    EXPECT_EQ(probe.rotationSaves, saves + 1);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    EXPECT_EQ(s.game.flow.gameState, GameState::ROTATION_EDITOR);

    s.step({Action::Confirm});
    s.step({Action::Back});
    EXPECT_EQ(probe.rotationSaves, saves + 1) << "nothing changed, nothing written";
}

// (20) Colors that match no stop read `C`, and an entered `C` steps back onto the walk: Left to the
// last ramp, Right to the first saved palette.
TEST(RotationEditor, ColorsThatMatchNothingReadCAndStepBackOntoTheWalk) {
    Probe probe;
    probe.rotations.working.levels = kirpich::render::kHotterRotation;
    ASSERT_FALSE(kirpich::systems::paletteChoiceOfColors(kirpich::render::kHotterRotation[0],
                                                         &probe.palettes));

    const auto content = spritesOf(draw(RotationEditorState{}, probe.rotations, probe.palettes));
    expectSprites(content,
                  kirpich::render::Glyphs("C", layout::cellX(0) + layout::kCellNumberDx,
                                          layout::cellY(0), layout::kGlyph, kAtlas, kPlayerRamp),
                  "C");

    Session s(probe);
    s.toLevel(0);
    s.step({Action::Confirm});
    EXPECT_FALSE(s.ui().entered);
    s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = kLastRamp}));
    s.step({Action::Back});
    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = true, .value = 1}));
}

// ── The picture ───────────────────────────────────────────────────────────────────────────────────

// (21) A cell's parts: the level's digit, the colon from the extras sheet, and the number reading the
// level's stop; the entered number through the selected font palette with its arrows exactly where a
// step exists, and no arrows on any other cell.
TEST(RotationEditor, ACellsParts) {
    Probe probe;
    for (std::size_t j = 0; j < 10; ++j) {
        probe.rotations.working.levels[j] = rampColors(static_cast<std::uint8_t>(j * 8));
    }
    probe.rotations.working.levels[9] = probe.palettes.slots[3].shades;

    const auto checkCells = [&](const RotationEditorState& ui) {
        const auto content = spritesOf(draw(ui, probe.rotations, probe.palettes));
        for (std::size_t j = 0; j < 10; ++j) {
            const int x = layout::cellX(j);
            const int y = layout::cellY(j);
            expectSprites(content,
                          kirpich::render::Glyphs(std::to_string(j), x + layout::kCellLevelDx, y,
                                                  8, kAtlas, kPlayerRamp),
                          "digit");
            const auto colon = named(content, glyphKey(x + layout::kCellColonDx, y));
            ASSERT_TRUE(colon) << "level " << j;
            EXPECT_EQ(colon->atlas, kAtlas.glyphExtras);
            EXPECT_EQ(colon->tile, kirpich::render::kColonCell);

            const std::string number = kirpich::systems::paletteChoiceText(
                kirpich::systems::levelPaletteChoice(ui, probe.rotations, &probe.palettes, j));
            const bool entered = ui.entering && ui.level == j;
            expectSprites(content,
                          entered ? kirpich::render::SelectedGlyphs(number, x + layout::kCellNumberDx,
                                                                    y, 8, kAtlas, kPlayerRamp)
                                  : kirpich::render::Glyphs(number, x + layout::kCellNumberDx, y, 8,
                                                            kAtlas, kPlayerRamp),
                          number);
        }
        const auto left  = named(content, "rotation-editor-number-left");
        const auto right = named(content, "rotation-editor-number-right");
        EXPECT_EQ(left.has_value(),
                  kirpich::systems::enteredNumberCanStep(ui, &probe.palettes, -1));
        EXPECT_EQ(right.has_value(),
                  kirpich::systems::enteredNumberCanStep(ui, &probe.palettes, +1));
        if (left) {
            EXPECT_EQ(left->x, layout::cellX(ui.level) + layout::kCellLeftArrowDx);
            EXPECT_EQ(left->y, layout::cellY(ui.level));
        }
        if (right) {
            EXPECT_EQ(right->x, layout::cellX(ui.level) + layout::kCellRightArrowDx);
            EXPECT_EQ(right->y, layout::cellY(ui.level));
        }
    };

    checkCells(RotationEditorState{});
    EXPECT_EQ(kirpich::systems::paletteChoiceText(kirpich::systems::levelPaletteChoice(
                  RotationEditorState{}, probe.rotations, &probe.palettes, 9)),
              "C04");
    EXPECT_EQ(kirpich::systems::paletteChoiceText(kirpich::systems::levelPaletteChoice(
                  RotationEditorState{}, probe.rotations, &probe.palettes, 1)),
              "9");

    for (const std::uint8_t level : {0, 4, 9}) {
        RotationEditorState ui{.row = RotationEditorRow::LEVELS, .level = level, .entering = true};
        ui.entered = kirpich::systems::paletteChoiceOfColors(probe.rotations.working.levels[level],
                                                             &probe.palettes);
        checkCells(ui);
    }
    // An entered number reads where it stands, not where the level's colors read back.
    RotationEditorState ui{.row = RotationEditorRow::LEVELS, .level = 2, .entering = true};
    ui.entered = PaletteChoice{.custom = true, .value = 7};
    EXPECT_EQ(kirpich::systems::paletteChoiceText(
                  kirpich::systems::levelPaletteChoice(ui, probe.rotations, &probe.palettes, 2)),
              "C07");
}

// (21a) The entered number's up and down arrows: Up over the number exactly where a step forward
// exists and Down under it exactly where one back does, centered on the number's field a pixel off its
// ink, turned from the selector; none at the walk's ends that way, and none on a cell not entered.
TEST(RotationEditor, TheEnteredNumberCarriesUpAndDownArrows) {
    Probe probe;
    const auto arrowsAt = [&](std::uint8_t level, PaletteChoice entered) {
        RotationEditorState ui{.row = RotationEditorRow::LEVELS, .level = level, .entering = true};
        ui.entered         = entered;
        const auto content = spritesOf(draw(ui, probe.rotations, probe.palettes));
        return std::pair{named(content, "rotation-editor-number-up"),
                         named(content, "rotation-editor-number-down")};
    };

    // The middle of the walk: both, at every level.
    for (std::uint8_t level = 0; level < 10; ++level) {
        const auto [up, down] = arrowsAt(level, PaletteChoice{.custom = false, .value = 40});
        ASSERT_TRUE(up) << "level " << +level;
        ASSERT_TRUE(down) << "level " << +level;
        const int x = layout::cellX(level) + 48;  // centered on the three-glyph field at 40
        EXPECT_EQ(up->x, x);
        EXPECT_EQ(down->x, x);
        EXPECT_EQ(up->y, layout::cellY(level) - 8);    // ink rows 3-6, one pixel over the number
        EXPECT_EQ(down->y, layout::cellY(level) + 7);  // ink rows 1-4, one pixel under it
        EXPECT_EQ(up->rotation, retropp::Rotation::Rot270);
        EXPECT_EQ(down->rotation, retropp::Rotation::Rot90);
        EXPECT_EQ(up->tile, down->tile) << "the same selector tile turned opposite ways";
    }

    // The first ramp: no step back, so no Down; the Up stays.
    {
        const auto [up, down] = arrowsAt(3, PaletteChoice{.custom = false, .value = 0});
        EXPECT_TRUE(up);
        EXPECT_FALSE(down);
    }
    // The walk's last stop - the last saved palette, slot 7 of the probe's - has no step forward.
    {
        const auto [up, down] = arrowsAt(3, PaletteChoice{.custom = true, .value = 7});
        EXPECT_FALSE(up);
        EXPECT_TRUE(down);
    }
    // Up stands exactly with Right and Down with Left, wherever the number is entered.
    for (const PaletteChoice entered : {PaletteChoice{.custom = false, .value = 0},
                                        PaletteChoice{.custom = false, .value = 79},
                                        PaletteChoice{.custom = true, .value = 1},
                                        PaletteChoice{.custom = true, .value = 7}}) {
        RotationEditorState ui{.row = RotationEditorRow::LEVELS, .level = 6, .entering = true};
        ui.entered         = entered;
        const auto content = spritesOf(draw(ui, probe.rotations, probe.palettes));
        EXPECT_EQ(named(content, "rotation-editor-number-up").has_value(),
                  named(content, "rotation-editor-number-right").has_value());
        EXPECT_EQ(named(content, "rotation-editor-number-down").has_value(),
                  named(content, "rotation-editor-number-left").has_value());
    }

    // No number entered: no up or down arrow anywhere.
    for (std::uint8_t level = 0; level < 10; ++level) {
        const auto content = spritesOf(draw(
            RotationEditorState{.row = RotationEditorRow::LEVELS, .level = level},
            probe.rotations, probe.palettes));
        EXPECT_FALSE(named(content, "rotation-editor-number-up"));
        EXPECT_FALSE(named(content, "rotation-editor-number-down"));
    }
}

// (22) The cursor: before the label on the four rows above the grid, before the cell in the grid, and
// nowhere while the blink is off or a number is entered.
TEST(RotationEditor, TheCursor) {
    const CustomRotations r = threeRotations();
    const CustomPalettes  p = threePalettes();
    const auto            hyphen =
        kirpich::render::Glyphs("-", 0, 0, 8, kAtlas, kPlayerRamp).front();

    const auto cursorAt = [&](const RotationEditorState& ui, bool blinkOn, int x, int y) {
        const auto content = spritesOf(draw(ui, r, p, blinkOn));
        const auto got     = named(content, glyphKey(x, y));
        return got && got->tile == hyphen.tile;
    };

    for (const RotationEditorRow row : {RotationEditorRow::PRESET, RotationEditorRow::LOAD,
                                        RotationEditorRow::SAVE, RotationEditorRow::CLEAR}) {
        EXPECT_TRUE(cursorAt(RotationEditorState{.row = row}, true, layout::kCursorX, rowY(row)));
        EXPECT_FALSE(cursorAt(RotationEditorState{.row = row}, false, layout::kCursorX, rowY(row)));
    }
    for (const std::uint8_t level : {0, 6, 9}) {
        const RotationEditorState ui{.row = RotationEditorRow::LEVELS, .level = level};
        const int x = layout::cellX(level) + layout::kCellCursorDx;
        const int y = layout::cellY(level);
        EXPECT_TRUE(cursorAt(ui, true, x, y)) << "level " << +level;
        EXPECT_FALSE(cursorAt(ui, false, x, y));
        EXPECT_FALSE(cursorAt(ui, true, layout::kCursorX, layout::kPresetY));
        RotationEditorState entering = ui;
        entering.entering            = true;
        EXPECT_FALSE(cursorAt(entering, true, x, y)) << "no cursor while a number is entered";
    }
}

// (23) The swatches are the working rotation's forty colors, level `i`'s at its cell with squares of
// the layout's side, opaque; a step recolors its own cell and no other.
TEST(RotationEditor, TheSwatchesAreTheWorkingRotationsColors) {
    Probe probe;
    probe.rotations.working           = distinct(20);
    probe.rotations.working.levels[3] = rampColors(30);

    const auto check = [](const kirpich::render::Layers& layers, const RotationProgression& w) {
        EXPECT_EQ(layers[1].regions.size(), 40u);
        for (std::size_t level = 0; level < 10; ++level) {
            const std::array<retropp::Rgba8, 4> colors{w.levels[level].darkest, w.levels[level].dark,
                                                       w.levels[level].light,
                                                       w.levels[level].lightest};
            for (std::size_t shade = 0; shade < 4; ++shade) {
                const std::string key = "rotation-editor-level-" + std::to_string(level) + "-" +
                                        std::to_string(shade);
                const retropp::Region* got = region(layers, key);
                ASSERT_NE(got, nullptr) << key;
                const auto x    = static_cast<float>(layout::cellSwatchX(level) +
                                                  static_cast<int>(shade) * layout::kCellSquare);
                const auto y    = static_cast<float>(layout::cellSwatchY(level));
                const auto side = static_cast<float>(layout::kCellSquare);
                ASSERT_EQ(got->shape.points.size(), 4u);
                EXPECT_EQ(got->shape.points[0], (retropp::Point{x, y})) << key;
                EXPECT_EQ(got->shape.points[2], (retropp::Point{x + side, y + side})) << key;
                retropp::Rgba8 fill = colors[shade];
                fill.a              = 255;
                EXPECT_EQ(got->effects[0].fill, fill) << key;
            }
        }
    };

    check(draw(RotationEditorState{}, probe.rotations, probe.palettes), probe.rotations.working);

    Session s(probe);
    s.toLevel(3);
    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    ASSERT_EQ(probe.rotations.working.levels[3], rampColors(31));
    RotationProgression want = distinct(20);
    want.levels[3]           = rampColors(31);
    check(draw(s.ui(), probe.rotations, probe.palettes), want);
}

// (24) The heading carries the asterisk exactly while the working rotation is unsaved.
TEST(RotationEditor, TheHeadingCarriesTheAsteriskWhileUnsaved) {
    CustomRotations r = threeRotations();
    const CustomPalettes p = threePalettes();
    const auto starred = [&p](const CustomRotations& rotations) {
        const auto content = spritesOf(draw(RotationEditorState{}, rotations, p));
        expectSprites(content,
                      kirpich::render::Glyphs(layout::kHeading, layout::kHeadingX, layout::kHeadingY,
                                              8, kAtlas, kPlayerRamp),
                      "heading");
        const auto star = named(content, glyphKey(layout::kAsteriskX, layout::kHeadingY));
        if (star) EXPECT_EQ(star->tile, kirpich::render::kAsteriskCell);
        return star.has_value();
    };
    EXPECT_FALSE(starred(r)) << "a fresh rotation has nothing to lose";
    r.working = distinct(99);
    EXPECT_TRUE(starred(r));
    r.working = r.slots[3].progression;
    EXPECT_FALSE(starred(r));
}

// (25) The held preview is up exactly while Select is held - the held set, not the pressed set - in
// every mode; while a number is entered, the name screen is up or a question is up, a held frame with
// a direction pressed changes nothing else.
TEST(RotationEditor, SelectHeldIsThePreviewInEveryMode) {
    Probe   probe;
    Session s(probe);
    s.holdFor({Action::Select});
    EXPECT_TRUE(s.ui().previewing);
    s.holdFor({});
    EXPECT_FALSE(s.ui().previewing);

    s.game.joypad.pressed = actionSet({Action::Select});
    s.game.joypad.held    = actionSet({});
    kirpich::systems::rotationEditor(s.game, s.wiring, s.rotations, s.palettes);
    EXPECT_FALSE(s.ui().previewing) << "a pressed-only frame is not a hold";

    s.holdFor({Action::MenuUp, Action::Confirm});
    EXPECT_FALSE(s.ui().previewing);

    // An entered number, the name screen and a question each keep their place under a held Select.
    probe.rotations.working.levels[0] = rampColors(10);
    s.toLevel(0);
    s.step({Action::Confirm});
    ASSERT_TRUE(s.ui().entering);
    const auto checkFrozen = [&s, &probe] {
        for (const Action a : {Action::MenuUp, Action::MenuDown, Action::MenuLeft, Action::MenuRight,
                               Action::Confirm, Action::Back}) {
            RotationEditorState   before    = s.ui();
            const CustomRotations rotations = probe.rotations;
            s.previewStep({a});
            EXPECT_TRUE(s.ui().previewing);
            before.previewing = true;
            EXPECT_EQ(s.ui(), before) << "action " << int(a);
            EXPECT_EQ(probe.rotations, rotations);
            EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        }
        s.holdFor({});
    };
    checkFrozen();
    s.step({Action::Back});

    s.toRow(RotationEditorRow::PRESET);
    s.toPreset(slot(2));
    s.toRow(RotationEditorRow::SAVE);
    s.step({Action::Confirm});
    ASSERT_TRUE(s.ui().naming);
    checkFrozen();
    s.step({Action::Back});
    ASSERT_FALSE(s.ui().naming);

    s.toPreset(slot(1));
    probe.rotations.working = distinct(99);
    s.toRow(RotationEditorRow::CLEAR);
    s.step({Action::Confirm});
    ASSERT_EQ(s.ui().question, RotationEditorQuestion::CLEAR);
    checkFrozen();
}

// (26) While Select is held in the walk the four directions step the preview's level - Up and Right
// forward, Down and Left back - with end stops at 0 and 9 that cue nothing; B does not leave and the
// row is unchanged; released in the grid, the cursor is on the level last shown.
TEST(RotationEditor, WhileHeldTheDirectionsStepTheLevel) {
    Probe   probe;
    Session s(probe);
    s.previewStep({Action::MenuUp});
    EXPECT_EQ(s.ui().level, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.previewStep({Action::MenuRight});
    EXPECT_EQ(s.ui().level, 2);
    s.previewStep({Action::MenuDown});
    EXPECT_EQ(s.ui().level, 1);
    s.previewStep({Action::MenuLeft});
    EXPECT_EQ(s.ui().level, 0);
    s.previewStep({Action::MenuLeft});
    EXPECT_EQ(s.ui().level, 0);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    for (int i = 0; i < 12; ++i) s.previewStep({Action::MenuUp});
    EXPECT_EQ(s.ui().level, 9);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    s.previewStep({Action::Back});
    EXPECT_EQ(s.game.flow.gameState, GameState::ROTATION_EDITOR);
    EXPECT_EQ(s.ui().row, RotationEditorRow::PRESET);
    s.holdFor({});
    EXPECT_EQ(s.ui().row, RotationEditorRow::PRESET) << "the row keeps its place";
    EXPECT_EQ(s.ui().level, 9) << "and the grid remembers the level";

    s.toLevel(2);
    for (int i = 0; i < 3; ++i) s.previewStep({Action::MenuUp});
    s.holdFor({});
    EXPECT_EQ(s.ui().row, RotationEditorRow::LEVELS);
    EXPECT_EQ(s.ui().level, 5);
}

// (27) The held preview is the one every screen shows: PreviewBackdrop's walls exactly, the heading,
// and PalettePreview with its `level` item reading the selected level - the item at the shared place,
// its arrows only where the level can still step.
TEST(RotationEditor, ThePreviewShowsTheLevelAsEveryPreviewDoes) {
    const CustomRotations r = threeRotations();
    const CustomPalettes  p = threePalettes();
    const auto            walls = kirpich::render::PreviewBackdrop(kAtlas, kRotationRamp);

    for (const std::uint8_t level : {0, 5, 9}) {
        const auto layers = draw(RotationEditorState{.level = level, .previewing = true}, r, p);
        const auto cells  = cellsOf(layers);
        ASSERT_EQ(cells.size(), walls.size());
        for (std::size_t i = 0; i < cells.size(); ++i) {
            EXPECT_EQ(cells[i].atlas, walls[i].atlas) << "level " << +level << " cell " << i;
            EXPECT_EQ(cells[i].tile, walls[i].tile) << "level " << +level << " cell " << i;
            EXPECT_EQ(cells[i].palette, walls[i].palette) << "level " << +level << " cell " << i;
        }

        const auto content = spritesOf(layers);
        const auto heading = kirpich::render::Glyphs(layout::kHeading, layout::kHeadingX,
                                                     layout::kHeadingY, 8, kAtlas, kRotationRamp);
        const auto preview = kirpich::render::PalettePreview(kAtlas, kRotationRamp, level);
        expectSprites(content, heading, "heading");
        expectSprites(content, preview, "preview");
        EXPECT_EQ(content.size(), heading.size() + preview.size());

        expectSprites(content,
                      kirpich::render::Glyphs(std::to_string(level),
                                              kirpich::render::kPalettePreviewLevelX +
                                                  kirpich::render::kLevelSelectorNumberDx,
                                              kirpich::render::kPalettePreviewLevelY, 8, kAtlas,
                                              kRotationRamp),
                      "the level's number");
        EXPECT_EQ(named(content, "preview-level-left").has_value(), level > 0);
        EXPECT_EQ(named(content, "preview-level-down").has_value(), level > 0);
        EXPECT_EQ(named(content, "preview-level-right").has_value(), level < 9);
        EXPECT_EQ(named(content, "preview-level-up").has_value(), level < 9);
    }
}

// (28) The preview draws through the rotation set whatever the settings say - the rotation off, a
// preset, CUSTOM, a ramp or the custom palette in use - and is the heading and the shared preview for
// the selected level exactly, with no cursor and no regions.
TEST(RotationEditor, ThePreviewDrawsThroughTheRotationSetWhateverTheSettings) {
    const CustomRotations r = threeRotations();
    const CustomPalettes  p = threePalettes();
    auto want = kirpich::render::Glyphs(layout::kHeading, layout::kHeadingX, layout::kHeadingY, 8,
                                        kAtlas, kRotationRamp);
    const auto preview = kirpich::render::PalettePreview(kAtlas, kRotationRamp, std::uint8_t{0});
    want.insert(want.end(), preview.begin(), preview.end());
    for (const PaletteRotation mode :
         {PaletteRotation::OFF, PaletteRotation::HOTTER, PaletteRotation::CUSTOM}) {
        for (const bool custom : {false, true}) {
            const Settings s{.shadeRamp = kPlayerRamp, .customPalette = custom,
                             .paletteRotation = mode};
            const auto layers = draw(RotationEditorState{.previewing = true}, r, p, true, s);
            ASSERT_EQ(layers.size(), 2u);
            EXPECT_EQ(layers[1].key.value, "rotation-editor-preview");
            const auto content = spritesOf(layers);
            ASSERT_EQ(content.size(), want.size());
            expectSprites(content, want, "the preview");
            EXPECT_TRUE(layers[1].regions.empty());
            for (const retropp::TileCell& cell : cellsOf(layers)) {
                EXPECT_TRUE(handlesOf(kAtlas.rotation).contains(cell.palette));
            }
        }
    }
}

// (29) The menu draws through the player's palette: every sprite and backdrop cell through
// paletteIndexOf's set, with a ramp and with the custom palette in use.
TEST(RotationEditor, TheMenuDrawsThroughThePlayersPalette) {
    CustomRotations r = threeRotations();
    r.working         = distinct(99);
    const CustomPalettes p = threePalettes();
    for (const Settings s : {Settings{.shadeRamp = kPlayerRamp},
                             Settings{.shadeRamp = kPlayerRamp, .customPalette = true}}) {
        const auto handles =
            handlesOf(kirpich::render::palettesFor(kAtlas, kirpich::paletteIndexOf(s)));
        const auto layers = draw(RotationEditorState{.row = RotationEditorRow::LEVELS,
                                                     .level = 3, .entering = true},
                                 r, p, true, s);
        for (const retropp::Sprite& sprite : spritesOf(layers)) {
            EXPECT_TRUE(handles.contains(sprite.palette)) << sprite.key.value;
        }
        for (const retropp::TileCell& cell : cellsOf(layers)) {
            EXPECT_TRUE(handles.contains(cell.palette));
        }
    }
}

// (30) Every frame names each object once and stays on screen: over the rows, every level, an entered
// number on every cell, each question, the name screen at each place, the held preview, and both blink
// states.
TEST(RotationEditor, EveryFrameNamesEachObjectOnceAndStaysOnScreen) {
    CustomRotations r = threeRotations();
    r.working         = distinct(99);
    r.slots[3].name   = nameOf("seven77");
    const CustomPalettes p = threePalettes();

    std::vector<RotationEditorState> states;
    for (const RotationEditorRow row : {RotationEditorRow::PRESET, RotationEditorRow::LOAD,
                                        RotationEditorRow::SAVE, RotationEditorRow::CLEAR}) {
        for (const RotationChoice preset : {shipped(PaletteRotation::RAINBOW), slot(4), slot(99)}) {
            states.push_back(RotationEditorState{.row = row, .preset = preset});
        }
    }
    for (std::uint8_t level = 0; level < 10; ++level) {
        states.push_back(RotationEditorState{.row = RotationEditorRow::LEVELS, .level = level});
        RotationEditorState entering{.row = RotationEditorRow::LEVELS, .level = level,
                                     .entering = true};
        entering.entered = PaletteChoice{.custom = false, .value = 40};
        states.push_back(entering);
        states.push_back(RotationEditorState{.level = level, .previewing = true});
    }
    for (const RotationEditorQuestion q : {RotationEditorQuestion::LOAD_UNSAVED,
                                           RotationEditorQuestion::SAVE_OVER,
                                           RotationEditorQuestion::CLEAR}) {
        states.push_back(RotationEditorState{.preset = slot(4), .question = q});
    }
    for (std::uint8_t place = 0; place < 7; ++place) {
        RotationEditorState naming{.naming = true, .namePlace = place};
        naming.typed = nameOf("ab3.-z9");
        states.push_back(naming);
    }

    for (const RotationEditorState& ui : states) {
        for (const bool blinkOn : {false, true}) {
            const auto            layers = draw(ui, r, p, blinkOn);
            std::set<std::string> keys;
            for (const retropp::Sprite& s : spritesOf(layers)) {
                EXPECT_TRUE(keys.insert(s.key.value).second) << "two objects named " << s.key.value;
                EXPECT_GE(s.x, 0) << s.key.value;
                EXPECT_GE(s.y, 0) << s.key.value;
                EXPECT_LE(s.x + 8, 160) << s.key.value;
                EXPECT_LE(s.y + 8, 144) << s.key.value;
            }
            for (const retropp::Region& reg : layers[1].regions) {
                EXPECT_TRUE(keys.insert(reg.key.value).second)
                    << "two objects named " << reg.key.value;
            }
        }
    }
}

// (31) The extra glyphs: the colon's plane against its art, the sheet holding the asterisk in cell 0
// and the colon in cell 1 side by side, and every font form drawing the colon from cell 1 through its
// own palette while the asterisk stays cell 0.
TEST(RotationEditor, TheExtraGlyphs) {
    EXPECT_EQ(kirpich::render::kAsteriskCell, 0);
    EXPECT_EQ(kirpich::render::kColonCell, 1);
    EXPECT_EQ(kirpich::render::kGlyphExtraCount, 4);

    const auto colon = kirpich::render::colonIndices();
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t col = 0; col < 8; ++col) {
            const bool ink = (row == 2 || row == 3 || row == 5 || row == 6) && (col == 3 || col == 4);
            EXPECT_EQ(colon[row * 8 + col], ink ? 0 : 1) << row << "," << col;
        }
    }

    const auto asterisk = kirpich::render::asteriskIndices();
    const auto sheet    = kirpich::render::glyphExtraSheet();
    ASSERT_EQ(sheet.size(), 32u * 8u);
    EXPECT_EQ(kirpich::render::kGlyphExtraSheetWidth, 32);
    EXPECT_EQ(kirpich::render::kGlyphExtraSheetHeight, 8);
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t col = 0; col < 8; ++col) {
            EXPECT_EQ(sheet[row * 32 + col], asterisk[row * 8 + col]) << "cell 0 " << row << "," << col;
            EXPECT_EQ(sheet[row * 32 + 8 + col], colon[row * 8 + col]) << "cell 1 " << row << "," << col;
        }
    }

    const RampPalettes& set = kAtlas.palettes[kPlayerRamp];
    struct Form {
        kirpich::render::Sprites sprites;
        retropp::PaletteId       palette;
    };
    for (const Form& f : {Form{kirpich::render::Glyphs(":", 0, 0, 8, kAtlas, kPlayerRamp),
                               set.fontSprite},
                          Form{kirpich::render::DimGlyphs(":", 0, 0, 8, kAtlas, kPlayerRamp),
                               set.fontSpriteDim},
                          Form{kirpich::render::SelectedGlyphs(":", 0, 0, 8, kAtlas, kPlayerRamp),
                               set.fontSpriteSelected}}) {
        ASSERT_EQ(f.sprites.size(), 1u);
        EXPECT_EQ(f.sprites[0].atlas, kAtlas.glyphExtras);
        EXPECT_EQ(f.sprites[0].tile, kirpich::render::kColonCell);
        EXPECT_EQ(f.sprites[0].palette, f.palette);
    }
    const auto star = kirpich::render::Glyphs("*", 0, 0, 8, kAtlas, kPlayerRamp);
    ASSERT_EQ(star.size(), 1u);
    EXPECT_EQ(star[0].tile, kirpich::render::kAsteriskCell);
}

// (32) Null wirings: with no rotations nothing steps, acts or enters, and B still leaves; with no
// palettes the entered walk is the ramps alone and ends at the last ramp.
TEST(RotationEditor, NullWirings) {
    {
        Probe   probe;
        Session s(probe, /*wired=*/false);
        for (const RotationEditorRow row :
             {RotationEditorRow::LOAD, RotationEditorRow::SAVE, RotationEditorRow::CLEAR}) {
            s.toRow(row);
            s.step({Action::Confirm});
            EXPECT_EQ(s.ui().question, RotationEditorQuestion::NONE);
            EXPECT_FALSE(s.ui().naming);
        }
        s.toLevel(4);
        s.step({Action::Confirm});
        EXPECT_FALSE(s.ui().entering);
        EXPECT_EQ(probe.rotationSaves, 0);
        s.step({Action::Back});
        EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_ROTATION);
    }
    {
        Probe probe;
        probe.rotations.working.levels[0] = rampColors(kLastRamp - 1);
        Session s(probe, /*wired=*/true, /*withPalettes=*/false);
        s.toLevel(0);
        s.step({Action::Confirm});
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = kLastRamp}));
        const int saves = probe.rotationSaves;
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui().entered, (PaletteChoice{.custom = false, .value = kLastRamp}));
        EXPECT_EQ(probe.rotationSaves, saves);
    }
}

// ── The name ──────────────────────────────────────────────────────────────────────────────────────

// (33) Save raises the name screen holding the slot's name, or its number: on empty slot 5 it opens
// on `5`, on a slot named `neon` on `neon`, at the first place with the key repeat zeroed and the
// screen-change cue, writing nothing; over a slot holding another rotation the question comes first
// and "no" raises nothing.
TEST(RotationEditor, SaveRaisesTheNameScreenHoldingTheSlotsNameOrNumber) {
    {
        Probe   probe;
        Session s(probe);
        s.toPreset(slot(5));
        s.toRow(RotationEditorRow::SAVE);
        s.game.flow.keyRepeatTimer = 4;
        s.step({Action::Confirm});
        EXPECT_TRUE(s.ui().naming);
        EXPECT_EQ(s.ui().typed, nameOf("5"));
        EXPECT_EQ(s.ui().namePlace, 0);
        EXPECT_EQ(s.game.flow.keyRepeatTimer, 0);
        EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
        EXPECT_FALSE(probe.rotations.slots[4].present);
        EXPECT_EQ(probe.rotationSaves, 0);
    }
    {
        Probe probe;
        probe.rotations.working       = probe.rotations.slots[3].progression;
        probe.rotations.slots[3].name = nameOf("neon");
        Session s(probe);
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        EXPECT_TRUE(s.ui().naming);
        EXPECT_EQ(s.ui().typed, nameOf("neon"));
    }
    {
        Probe probe;
        probe.rotations.working = probe.rotations.slots[3].progression;
        Session s(probe);
        s.toPreset(slot(1));
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        EXPECT_EQ(s.ui().question, RotationEditorQuestion::SAVE_OVER);
        s.step({Action::Confirm});  // "no"
        EXPECT_FALSE(s.ui().naming);
        s.step({Action::Confirm});
        s.step({Action::MenuRight});
        s.step({Action::Confirm});  // "yes"
        EXPECT_TRUE(s.ui().naming);
        EXPECT_EQ(s.ui().typed, nameOf("1"));
        EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
        EXPECT_EQ(probe.rotationSaves, 0);
    }
}

// (34) The wheel: up from `a` through `z`, `.`, `-`, `0` through `9`, the space and back to `a`, and
// the exact reverse going down; a character the wheel does not hold turns as the space does.
TEST(RotationEditor, TheNameWheel) {
    const std::string_view ring = "abcdefghijklmnopqrstuvwxyz.-0123456789 ";
    ASSERT_EQ(ring.size(), 39u);
    for (std::size_t i = 0; i < ring.size(); ++i) {
        EXPECT_EQ(kirpich::systems::turnedNameWheel(ring[i], +1), ring[(i + 1) % ring.size()])
            << "up from '" << ring[i] << "'";
        EXPECT_EQ(kirpich::systems::turnedNameWheel(ring[i], -1),
                  ring[(i + ring.size() - 1) % ring.size()])
            << "down from '" << ring[i] << "'";
    }
    for (const char c : {'#', 'A', '*'}) {
        EXPECT_EQ(kirpich::systems::turnedNameWheel(c, +1), 'a');
        EXPECT_EQ(kirpich::systems::turnedNameWheel(c, -1), '9');
    }
}

// (35) Up and Down turn the character at the place, Up read before Down, on the key repeat, a tink a
// turn; the other places, the slot and the document untouched.
TEST(RotationEditor, UpAndDownTurnTheCharacterAtThePlace) {
    Probe   probe;
    Session s(probe);
    s.toPreset(slot(5));
    s.toRow(RotationEditorRow::SAVE);
    s.step({Action::Confirm});
    s.step({Action::Confirm});
    s.step({Action::Confirm});
    ASSERT_EQ(s.ui().namePlace, 2);
    ASSERT_EQ(s.ui().typed, nameOf("5aa"));

    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().typed, nameOf("5ab"));
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().typed, nameOf("5aa"));
    s.step({Action::MenuUp, Action::MenuDown});
    EXPECT_EQ(s.ui().typed, nameOf("5ab")) << "Up is read before Down";
    s.step({Action::MenuDown});

    std::vector<int> turned;
    for (int frame = 1; frame <= 60; ++frame) {
        const RotationName before = s.ui().typed;
        if (frame == 1) {
            s.step({Action::MenuUp});
        } else {
            s.holdFor({Action::MenuUp});
        }
        if (s.ui().typed != before) turned.push_back(frame);
    }
    EXPECT_EQ(turned, (std::vector<int>{1, 24, 33, 42, 51, 60}));
    EXPECT_EQ(s.ui().typed[0], '5');
    EXPECT_EQ(s.ui().typed[1], 'a');
    for (std::size_t i = 3; i < 7; ++i) EXPECT_EQ(s.ui().typed[i], ' ');
    EXPECT_FALSE(probe.rotations.slots[4].present);
    EXPECT_EQ(probe.rotationSaves, 0);
}

// (36) A moves to the next place with the screen-change cue, a blank place reached becoming `a`, and
// finishes on the seventh; Start finishes from any place; B moves back without a cue; Left and Right
// do nothing.
TEST(RotationEditor, AMovesOnBMovesBackStartFinishes) {
    {
        Probe   probe;
        Session s(probe);
        s.toPreset(slot(5));
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        s.step({Action::Confirm});
        EXPECT_EQ(s.ui().namePlace, 1);
        EXPECT_EQ(s.ui().typed, nameOf("5a"));
        EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);

        const RotationEditorState before = s.ui();
        s.step({Action::MenuLeft});
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui(), before);

        s.step({Action::Back});
        EXPECT_EQ(s.ui().namePlace, 0);
        EXPECT_EQ(s.sound(), SquareSfxId::NONE);
        EXPECT_TRUE(s.ui().naming);

        for (int i = 0; i < 6; ++i) s.step({Action::Confirm});
        EXPECT_EQ(s.ui().namePlace, 6);
        EXPECT_TRUE(s.ui().naming);
        s.step({Action::Confirm});
        EXPECT_FALSE(s.ui().naming) << "A on the seventh place finishes";
        EXPECT_EQ(probe.rotations.slots[4].name, nameOf("5aaaaaa"));
    }
    {
        Probe probe;
        probe.rotations.working       = probe.rotations.slots[3].progression;
        probe.rotations.slots[3].name = nameOf("neon");
        Session s(probe);
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        for (int i = 0; i < 4; ++i) s.step({Action::Confirm});
        EXPECT_EQ(s.ui().typed, nameOf("neona")) << "the place after the name starts at a";
        s.step({Action::Back});
        s.step({Action::Back});
        ASSERT_EQ(s.ui().namePlace, 2);
        s.step({Action::Start});
        EXPECT_FALSE(s.ui().naming) << "Start finishes from the third place";
        EXPECT_EQ(probe.rotations.slots[3].name, nameOf("neona"));
    }
}

// (37) Finishing is the save: the slot present, holding the working rotation under the name typed,
// the document written once, a tink, the screen down. A name finished as seven spaces is stored as the
// slot's number. Saving again onto the slot that holds the rotation changes its name and nothing else.
TEST(RotationEditor, FinishingIsTheSave) {
    {
        Probe probe;
        probe.rotations.working = distinct(99);
        Session s(probe);
        s.toPreset(slot(5));
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        s.step({Action::MenuDown});  // `5` down to `4`
        s.step({Action::Start});
        EXPECT_FALSE(s.ui().naming);
        EXPECT_EQ(probe.rotations.slots[4],
                  (CustomRotationSlot{.present     = true,
                                      .name        = nameOf("4"),
                                      .progression = distinct(99)}));
        EXPECT_EQ(probe.rotationSaves, 1);
        EXPECT_EQ(s.sound(), SquareSfxId::TINK);
        EXPECT_EQ(kirpich::savedSlotOf(probe.rotations), std::optional<std::uint8_t>{5});
    }
    {
        Probe probe;
        probe.rotations.working = distinct(99);
        Session s(probe);
        s.toPreset(slot(5));
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        for (int i = 0; i < 5; ++i) s.step({Action::MenuUp});  // `5` up through `9` to the space
        ASSERT_EQ(s.ui().typed, kirpich::blankRotationName());
        s.step({Action::Start});
        EXPECT_EQ(probe.rotations.slots[4].name, nameOf("5")) << "a blank name is the slot's number";
    }
    {
        Probe probe;
        probe.rotations.working       = probe.rotations.slots[3].progression;
        probe.rotations.slots[3].name = nameOf("neon");
        CustomRotations want          = probe.rotations;
        want.slots[3].name            = nameOf("oeon");
        Session s(probe);
        s.toRow(RotationEditorRow::SAVE);
        s.step({Action::Confirm});
        s.step({Action::MenuUp});
        s.step({Action::Start});
        EXPECT_EQ(probe.rotations, want);
        EXPECT_EQ(probe.rotationSaves, 1);
    }
}

// (38) B on the first place cancels the save: the slot, its name and the rotations as they were, the
// document never written, the screen-change cue, and the cursor back on `save preset`.
TEST(RotationEditor, BOnTheFirstPlaceCancelsTheSave) {
    Probe probe;
    probe.rotations.working       = probe.rotations.slots[3].progression;
    probe.rotations.slots[3].name = nameOf("neon");
    const CustomRotations before  = probe.rotations;
    Session s(probe);
    s.toRow(RotationEditorRow::SAVE);
    s.step({Action::Confirm});
    s.step({Action::MenuUp});
    s.step({Action::Back});
    EXPECT_FALSE(s.ui().naming);
    EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
    EXPECT_EQ(probe.rotations, before);
    EXPECT_EQ(probe.rotationSaves, 0);
    EXPECT_EQ(s.ui().row, RotationEditorRow::SAVE);
    EXPECT_EQ(s.game.flow.gameState, GameState::ROTATION_EDITOR);
}

// (39) The selector reads names: a named slot its name, an unnamed one its number without a leading
// zero, an empty one dimmed, the shipped three theirs; and a cleared named slot reads its number again.
TEST(RotationEditor, TheSelectorReadsNames) {
    CustomRotations r = threeRotations();
    r.slots[3].name   = nameOf("neon");
    const CustomPalettes p = threePalettes();

    struct Case {
        RotationChoice preset;
        std::string    text;
        bool           dim;
    };
    for (const Case& c : {Case{slot(4), "neon", false}, Case{slot(1), "1", false},
                          Case{slot(12), "12", true}, Case{shipped(PaletteRotation::HOTTER), "hotter", false},
                          Case{shipped(PaletteRotation::COLDER), "colder", false},
                          Case{shipped(PaletteRotation::RAINBOW), "rainbow", false}}) {
        EXPECT_EQ(kirpich::systems::rotationPresetText(c.preset, r), c.text);
        const auto content = spritesOf(draw(RotationEditorState{.preset = c.preset}, r, p));
        expectSprites(content,
                      c.dim ? kirpich::render::DimGlyphs(c.text, layout::kPresetValueX,
                                                         layout::kPresetY, 8, kAtlas, kPlayerRamp)
                            : kirpich::render::Glyphs(c.text, layout::kPresetValueX, layout::kPresetY,
                                                      8, kAtlas, kPlayerRamp),
                      c.text);
    }

    Probe probe;
    probe.rotations.slots[3].name = nameOf("neon");
    Session s(probe);
    s.toPreset(slot(4));
    s.toRow(RotationEditorRow::CLEAR);
    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    s.step({Action::Confirm});
    EXPECT_EQ(kirpich::systems::rotationPresetText(slot(4), probe.rotations), "4");
}

// (40) The text input screen as a component: the title centered on its line; the text's glyphs
// centered as a run through the selected font palette; the glyph at the place present with the blink
// on and absent with it off; the two arrows over and under that place; every sprite on screen for
// every place. And while it is up, the editor's frame is the backdrop and that layer alone.
TEST(RotationEditor, TheTextEntryComponent) {
    constexpr std::string_view kText = "ab3.-z9";
    const int textX = kirpich::render::textEntryTextX(kText.size());
    EXPECT_EQ(textX, 52);
    EXPECT_EQ(kirpich::render::textEntryTextX(11), 36);

    for (std::size_t place = 0; place < kText.size(); ++place) {
        for (const bool blinkOn : {false, true}) {
            const retropp::DrawLayer layer = kirpich::render::TextEntry(
                "name", "save preset", kText, place, blinkOn, kAtlas, kPlayerRamp);
            EXPECT_EQ(layer.key.value, "name");
            EXPECT_EQ(layer.z, kirpich::render::kTextEntryZ);
            const auto content = std::get<retropp::SpriteContent>(layer.content).sprites;

            expectSprites(content,
                          kirpich::render::Glyphs("save preset", 36, kirpich::render::kTextEntryTitleY,
                                                  8, kAtlas, kPlayerRamp),
                          "title");
            for (std::size_t i = 0; i < kText.size(); ++i) {
                const int  x       = textX + static_cast<int>(i) * 8;
                const auto glyph   = named(content, glyphKey(x, kirpich::render::kTextEntryTextY));
                const bool visible = i != place || blinkOn;
                EXPECT_EQ(glyph.has_value(), visible) << "place " << place << " glyph " << i;
                if (glyph) {
                    expectSprites(content,
                                  kirpich::render::SelectedGlyphs(kText.substr(i, 1), x,
                                                                  kirpich::render::kTextEntryTextY,
                                                                  8, kAtlas, kPlayerRamp),
                                  "text");
                }
            }
            const int  placeX = textX + static_cast<int>(place) * 8;
            const auto up     = named(content, "name-up");
            const auto down   = named(content, "name-down");
            ASSERT_TRUE(up);
            ASSERT_TRUE(down);
            EXPECT_EQ(up->x, placeX);
            EXPECT_EQ(up->y, kirpich::render::kTextEntryTextY - kirpich::render::kTextEntryArrowRise);
            EXPECT_EQ(down->x, placeX);
            EXPECT_EQ(down->y, kirpich::render::kTextEntryTextY + kirpich::render::kTextEntryArrowRise);
            expectSprites(content,
                          kirpich::render::Arrow("name-up", placeX, up->y,
                                                 kirpich::render::ArrowDirection::UP, up->z, kAtlas,
                                                 kPlayerRamp),
                          "up");
            expectSprites(content,
                          kirpich::render::Arrow("name-down", placeX, down->y,
                                                 kirpich::render::ArrowDirection::DOWN, down->z,
                                                 kAtlas, kPlayerRamp),
                          "down");
            for (const retropp::Sprite& s : content) {
                EXPECT_GE(s.x, 0);
                EXPECT_LE(s.x + 8, 160);
                EXPECT_GE(s.y, 0);
                EXPECT_LE(s.y + 8, 144);
            }
        }
    }

    RotationEditorState ui{.naming = true, .namePlace = 3};
    ui.typed = nameOf("neon");
    const auto layers = draw(ui, threeRotations(), threePalettes(), true);
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].key.value, "rotation-editor-backdrop");
    EXPECT_EQ(layers[1].key.value, "rotation-editor-name");
    EXPECT_TRUE(layers[1].regions.empty());
    const auto plain = kirpich::render::Backdrop(kAtlas, kPlayerRamp);
    const auto cells = cellsOf(layers);
    ASSERT_EQ(cells.size(), plain.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        EXPECT_EQ(cells[i].tile, plain[i].tile);
        EXPECT_EQ(cells[i].palette, plain[i].palette);
    }
    expectSprites(spritesOf(layers),
                  kirpich::render::Glyphs(layout::kNameTitle, 36, kirpich::render::kTextEntryTitleY,
                                          8, kAtlas, kPlayerRamp),
                  "the name screen's title");
}
