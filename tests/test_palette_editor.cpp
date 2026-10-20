// The palette editor - behavioral tests over its logic (src/render/screens/palette_editor/
// palette_editor.h), its picture (screen.h beside it), and the shared components it is built from
// (src/render/components/arrow.h, page_arrows.h, glyphs.h) and the atlas palette and glyph it adds
// (src/render/tile_atlas.h, src/render/glyph_extras.h).
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
#include <kirpich/game_state.h>

#include "render/components/arrow.h"
#include "render/components/glyphs.h"
#include "render/components/page_arrows.h"
#include "render/glyph_extras.h"
#include "render/palettes.h"
#include "render/screens/palette_editor/layout.h"
#include "render/screens/palette_editor/palette_editor.h"
#include "render/screens/palette_editor/screen.h"
#include "render/settings_overlay.h"
#include "render/tile_atlas.h"
#include "retropp/input.h"
#include "state/custom_palettes.h"
#include "state/screen_ui_state.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/list_screen.h"
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
using kirpich::render::ArrowDirection;
using kirpich::render::kShadeRampCount;
using kirpich::render::ShadeRamp;
using kirpich::render::TileAtlas;
using kirpich::systems::CustomPaletteWiring;
using kirpich::systems::GameContext;
using kirpich::systems::SettingsWiring;

constexpr std::uint8_t kPlayerRamp = 7;
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
    fill(atlas.custom, kirpich::render::kCustomRamp);
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
    Settings                 settings{.shadeRamp = kPlayerRamp};
    CustomPalettes           palettes{};
    int                      applies = 0;
    int                      saves   = 0;
    std::optional<ShadeRamp> lastApplied;
    std::vector<std::string> events;

    SettingsWiring wiring() { return SettingsWiring{.settings = &settings}; }

    CustomPaletteWiring custom() {
        return CustomPaletteWiring{
            .palettes = &palettes,
            .apply =
                [this](const ShadeRamp& shades) {
                    ++applies;
                    lastApplied = shades;
                    events.emplace_back("apply");
                },
            .save =
                [this](const CustomPalettes&) {
                    ++saves;
                    events.emplace_back("save");
                },
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

// The run `text` drawn at (x, y): every glyph there, with the tile and palette `expected` gives it.
void expectRun(std::span<const retropp::Sprite> content, const kirpich::render::Sprites& expected,
               std::string_view what) {
    for (const retropp::Sprite& want : expected) {
        const auto got = named(content, want.key.value);
        ASSERT_TRUE(got) << what << " at " << want.key.value;
        EXPECT_EQ(got->tile, want.tile) << what;
        EXPECT_EQ(got->atlas, want.atlas) << what;
        EXPECT_EQ(got->palette, want.palette) << what;
    }
}

std::string padded(unsigned value, std::size_t width) {
    std::string text = std::to_string(value);
    return std::string(width > text.size() ? width - text.size() : 0, '0') + text;
}

constexpr std::array kChannels{ColorChannel::RED, ColorChannel::GREEN, ColorChannel::BLUE};

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

// Opening puts every part of the state back, whatever the last visit left, and seeds the preset with
// the slot the working palette is saved in - or slot 1 when it is saved in none, or when there are no
// palettes behind the wiring.
TEST(PaletteEditor, OpeningResetsTheStateAndSeedsThePreset) {
    CustomPalettes p = threeSlots();
    p.working        = p.slots[3].shades;

    GameContext game;
    game.paletteEditor = PaletteEditorState{.row          = PaletteEditorRow::SHADE_3,
                                            .channel      = ColorChannel::BLUE,
                                            .preset       = 50,
                                            .entering     = true,
                                            .place        = DigitPlace::ONES,
                                            .valueOnEntry = 9,
                                            .question     = PaletteEditorQuestion::CLEAR,
                                            .answerYes    = true};
    game.screens.cursorVisible = false;

    kirpich::systems::openPaletteEditor(game, &p);
    EXPECT_EQ(game.paletteEditor, (PaletteEditorState{.preset = 4}));
    EXPECT_TRUE(game.screens.cursorVisible);
    EXPECT_EQ(game.flow.timer1, kBlinkFrames);
    EXPECT_EQ(game.flow.gameState, GameState::PALETTE_EDITOR);

    p.working = rampFrom(99);  // in no slot
    kirpich::systems::openPaletteEditor(game, &p);
    EXPECT_EQ(game.paletteEditor.preset, 1);

    kirpich::systems::openPaletteEditor(game, nullptr);
    EXPECT_EQ(game.paletteEditor.preset, 1);
}

// The walk: eight rows with an end stop at the bottom, a cue on every move and none at a stop; Up on
// the first row and B from any row return to the Palette screen; Left and Right move the channel on
// a shade row, and the channel survives a trip through the rows above; on an action row they are end
// stops.
TEST(PaletteEditor, TheWalk) {
    Probe   probe;
    Session s(probe);
    ASSERT_EQ(s.game.flow.gameState, GameState::PALETTE_EDITOR);

    for (std::size_t i = 1; i < kirpich::kPaletteEditorRowCount; ++i) {
        s.step({Action::MenuDown});
        EXPECT_EQ(static_cast<std::size_t>(s.ui().row), i);
        EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    }
    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().row, PaletteEditorRow::SHADE_4) << "the last row is an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    s.step({Action::MenuRight});
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().channel, ColorChannel::BLUE);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().channel, ColorChannel::BLUE) << "blue is an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    while (s.ui().row != PaletteEditorRow::SAVE) s.step({Action::MenuUp});
    s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().channel, ColorChannel::BLUE) << "an action row is an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    s.step({Action::MenuRight});
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    s.toRow(PaletteEditorRow::SHADE_1);
    EXPECT_EQ(s.ui().channel, ColorChannel::BLUE) << "the channel is kept";
    s.step({Action::MenuLeft});
    s.step({Action::MenuLeft});
    s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().channel, ColorChannel::RED);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE) << "red is an end stop";

    s.step({Action::Back});
    EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_SETTINGS);
    EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);

    Session t(probe);
    t.step({Action::MenuUp});
    EXPECT_EQ(t.game.flow.gameState, GameState::PALETTE_SETTINGS) << "Up past the first row";
    EXPECT_EQ(t.sound(), SquareSfxId::CHANGE_SCREEN);
    EXPECT_EQ(t.game.screens.settingsRow, SettingsRow::PALETTE)
        << "the settings screen's cursor is untouched";
}

// The preset selector runs 1 to 99 with an end stop at each end and a cue on every move; A on it is
// an end stop.
TEST(PaletteEditor, ThePresetSelector) {
    Probe   probe;
    Session s(probe);
    ASSERT_EQ(s.ui().preset, 1);

    s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().preset, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    for (int n = 2; n <= 99; ++n) {
        s.step({Action::MenuRight});
        EXPECT_EQ(s.ui().preset, n);
        EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    }
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().preset, 99);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    for (int n = 98; n >= 1; --n) s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().preset, 1);

    const PaletteEditorState before = s.ui();
    s.step({Action::Confirm});
    EXPECT_EQ(s.ui(), before);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    EXPECT_EQ(probe.saves, 0);
}

// A on a value enters it with the place on the hundreds and the value it had recorded, and cues
// nothing; while entering, Down does not move the row and B does not leave the screen.
TEST(PaletteEditor, EnteringAValue) {
    Probe probe;
    kirpich::setChannelValue(probe.palettes.working, 1, ColorChannel::GREEN, 123);
    Session s(probe);
    s.toRow(PaletteEditorRow::SHADE_2);
    s.step({Action::MenuRight});

    s.step({Action::Confirm});
    EXPECT_TRUE(s.ui().entering);
    EXPECT_EQ(s.ui().place, DigitPlace::HUNDREDS);
    EXPECT_EQ(s.ui().valueOnEntry, 123);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().row, PaletteEditorRow::SHADE_2) << "Down steps the value, not the row";
    s.step({Action::Back});
    EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_EDITOR) << "B leaves the value, not the screen";
    EXPECT_FALSE(s.ui().entering);
}

// The stepping law: one clamped value stepped by its place. From 099 at the hundreds Up gives 199,
// then 255, then nothing; from 005 at the ones Down five times reaches 000 and then stops; at the
// tens 250 goes to 255. Every change writes the channel, fires apply with the new palette and then
// save, and cues; an end stop fires nothing.
TEST(PaletteEditor, TheSteppingLawClampsAndCommitsInOrder) {
    Probe probe;
    kirpich::setChannelValue(probe.palettes.working, 0, ColorChannel::RED, 99);
    Session s(probe);
    s.toRow(PaletteEditorRow::SHADE_1);
    s.step({Action::Confirm});
    probe.events.clear();

    s.step({Action::MenuUp});
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 0, ColorChannel::RED), 199);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    ASSERT_TRUE(probe.lastApplied);
    EXPECT_EQ(*probe.lastApplied, probe.palettes.working);
    EXPECT_EQ(probe.events, (std::vector<std::string>{"apply", "save"}));

    s.step({Action::MenuUp});
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 0, ColorChannel::RED), 255);

    const int applies = probe.applies;
    s.step({Action::MenuUp});
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 0, ColorChannel::RED), 255);
    EXPECT_EQ(probe.applies, applies) << "an end stop fires nothing";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    // 005 at the ones.
    kirpich::setChannelValue(probe.palettes.working, 0, ColorChannel::RED, 5);
    s.step({Action::MenuRight});
    s.step({Action::MenuRight});
    ASSERT_EQ(s.ui().place, DigitPlace::ONES);
    for (int v = 4; v >= 0; --v) {
        s.step({Action::MenuDown});
        EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 0, ColorChannel::RED), v);
    }
    const int saves = probe.saves;
    s.step({Action::MenuDown});
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 0, ColorChannel::RED), 0);
    EXPECT_EQ(probe.saves, saves);

    // 250 at the tens.
    kirpich::setChannelValue(probe.palettes.working, 0, ColorChannel::RED, 250);
    s.step({Action::MenuLeft});
    ASSERT_EQ(s.ui().place, DigitPlace::TENS);
    s.step({Action::MenuUp});
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 0, ColorChannel::RED), 255);

    EXPECT_EQ(kirpich::systems::steppedValue(99, DigitPlace::HUNDREDS, +1), 199);
    EXPECT_EQ(kirpich::systems::steppedValue(199, DigitPlace::HUNDREDS, +1), 255);
    EXPECT_EQ(kirpich::systems::steppedValue(30, DigitPlace::HUNDREDS, -1), 0);
}

// Left and Right move the place with an end stop at each end and a cue on every move.
TEST(PaletteEditor, ThePlaceMovesWithEndStops) {
    Probe   probe;
    Session s(probe);
    s.toRow(PaletteEditorRow::SHADE_1);
    s.step({Action::Confirm});

    s.step({Action::MenuLeft});
    EXPECT_EQ(s.ui().place, DigitPlace::HUNDREDS);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().place, DigitPlace::TENS);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().place, DigitPlace::ONES);
    s.step({Action::MenuRight});
    EXPECT_EQ(s.ui().place, DigitPlace::ONES);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
}

// Up held: a step on the press, the next 23 frames later, then one every 9; a release and a fresh
// press steps at once.
TEST(PaletteEditor, AHeldDirectionRepeatsOnTheGamesKeyRepeat) {
    Probe   probe;
    Session s(probe);
    s.toRow(PaletteEditorRow::SHADE_1);
    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    s.step({Action::MenuRight});  // the ones

    std::vector<int> stepped;
    for (int frame = 1; frame <= 60; ++frame) {
        const int before = probe.applies;
        if (frame == 1) {
            s.step({Action::MenuUp});
        } else {
            s.holdFor({Action::MenuUp});
        }
        if (probe.applies != before) stepped.push_back(frame);
    }
    EXPECT_EQ(stepped, (std::vector<int>{1, 24, 33, 42, 51, 60}));

    s.holdFor({});
    const int before = probe.applies;
    s.step({Action::MenuUp});
    EXPECT_EQ(probe.applies, before + 1) << "a fresh press steps at once";
}

// A keeps the value and leaves entering with a cue. B puts back the value it had when it was
// entered, through apply and save, without a cue - and with the value unchanged fires nothing.
TEST(PaletteEditor, AKeepsTheValueAndBPutsItBack) {
    Probe probe;
    kirpich::setChannelValue(probe.palettes.working, 2, ColorChannel::BLUE, 40);
    Session s(probe);
    s.toRow(PaletteEditorRow::SHADE_3);
    s.step({Action::MenuRight});
    s.step({Action::MenuRight});

    s.step({Action::Confirm});
    s.step({Action::MenuUp});
    ASSERT_EQ(kirpich::channelValue(probe.palettes.working, 2, ColorChannel::BLUE), 140);
    s.step({Action::Confirm});
    EXPECT_FALSE(s.ui().entering);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 2, ColorChannel::BLUE), 140);

    s.step({Action::Confirm});
    s.step({Action::MenuUp});
    ASSERT_EQ(kirpich::channelValue(probe.palettes.working, 2, ColorChannel::BLUE), 240);
    probe.events.clear();
    s.step({Action::Back});
    EXPECT_FALSE(s.ui().entering);
    EXPECT_EQ(kirpich::channelValue(probe.palettes.working, 2, ColorChannel::BLUE), 140);
    EXPECT_EQ(probe.events, (std::vector<std::string>{"apply", "save"}));
    ASSERT_TRUE(probe.lastApplied);
    EXPECT_EQ(*probe.lastApplied, probe.palettes.working);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    s.step({Action::Confirm});
    probe.events.clear();
    s.step({Action::Back});
    EXPECT_TRUE(probe.events.empty()) << "nothing changed, so nothing is written";
}

// Load: with unsaved changes, A asks first and only yes loads; saved, or the untouched grayscale, it
// loads at once; an empty slot and a slot holding the working palette already are end stops.
TEST(PaletteEditor, LoadAsksWhileTheWorkingPaletteIsUnsaved) {
    Probe probe;
    probe.palettes         = threeSlots();
    probe.palettes.working = rampFrom(99);  // in no slot
    Session s(probe);
    s.toRow(PaletteEditorRow::LOAD);

    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::LOAD_UNSAVED);
    EXPECT_FALSE(s.ui().answerYes);
    EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
    s.step({Action::Confirm});  // no
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);
    EXPECT_EQ(probe.palettes.working, rampFrom(99));

    s.step({Action::Confirm});
    s.step({Action::Back});
    EXPECT_EQ(probe.palettes.working, rampFrom(99)) << "B is no";

    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    probe.events.clear();
    s.step({Action::Confirm});  // yes
    EXPECT_EQ(probe.palettes.working, rampFrom(10));
    EXPECT_EQ(probe.events, (std::vector<std::string>{"apply", "save"}));
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);

    // Now saved in slot 1: slot 1 is an end stop, and slot 4 loads with no question.
    probe.events.clear();
    s.step({Action::Confirm});
    EXPECT_TRUE(probe.events.empty());
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);

    s.ui().preset = 4;
    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);
    EXPECT_EQ(probe.palettes.working, rampFrom(40));
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);

    // An empty slot has nothing to load, whatever the working palette holds.
    probe.palettes.working = rampFrom(99);
    probe.events.clear();
    s.ui().preset = 2;
    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);
    EXPECT_EQ(probe.palettes.working, rampFrom(99));
    EXPECT_TRUE(probe.events.empty());
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    // The untouched grayscale is in no slot but has nothing to lose, so loading over it asks nothing.
    probe.palettes.working = kirpich::render::freshCustomPalette();
    s.ui().preset          = 4;
    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);
    EXPECT_EQ(probe.palettes.working, rampFrom(40));
}

// Save: an empty slot is written at once with the document saved and the atlas untouched; a present,
// different slot asks, yes replaces and no leaves it; a slot already holding the working palette is
// an end stop with no question.
TEST(PaletteEditor, SaveAsksOnlyOverADifferentPalette) {
    Probe probe;
    probe.palettes         = threeSlots();
    probe.palettes.working = rampFrom(99);
    Session s(probe);
    s.toRow(PaletteEditorRow::SAVE);

    s.ui().preset = 2;
    s.step({Action::Confirm});
    EXPECT_EQ(probe.palettes.slots[1], (CustomPaletteSlot{.present = true, .shades = rampFrom(99)}));
    EXPECT_EQ(probe.saves, 1);
    EXPECT_EQ(probe.applies, 0);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);

    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE) << "the slot holds it already";
    EXPECT_EQ(probe.saves, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    s.ui().preset = 4;
    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::SAVE_OVER);
    s.step({Action::Confirm});  // no
    EXPECT_EQ(probe.palettes.slots[3].shades, rampFrom(40));

    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    s.step({Action::Confirm});  // yes
    EXPECT_EQ(probe.palettes.slots[3].shades, rampFrom(99));
    EXPECT_EQ(probe.applies, 0);
}

// Clear: a present slot asks, yes empties it and no leaves it; an empty slot is an end stop. Clearing
// the slot that holds the palette in use puts the game back on the ramp underneath, with the working
// palette left as it was; clearing any other slot, or one whose palette another slot also holds,
// leaves the settings alone.
TEST(PaletteEditor, ClearAsksAndEmptiesTheSlot) {
    Probe probe;
    probe.palettes         = threeSlots();
    probe.palettes.working = probe.palettes.slots[6].shades;
    probe.settings         = Settings{.shadeRamp = kPlayerRamp, .customPalette = true};
    Session s(probe);
    ASSERT_EQ(s.ui().preset, 7);
    s.toRow(PaletteEditorRow::CLEAR);

    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::CLEAR);
    s.step({Action::Confirm});  // no
    EXPECT_TRUE(probe.palettes.slots[6].present);
    EXPECT_TRUE(probe.settings.customPalette);

    s.step({Action::Confirm});
    s.step({Action::MenuRight});
    s.step({Action::Confirm});  // yes
    EXPECT_EQ(probe.palettes.slots[6], CustomPaletteSlot{});
    EXPECT_EQ(probe.palettes.working, rampFrom(70)) << "the working palette is untouched";
    EXPECT_FALSE(probe.settings.customPalette) << "the palette in use is gone, so the ramp returns";
    EXPECT_EQ(probe.settings.shadeRamp, kPlayerRamp) << "the ramp underneath";
    EXPECT_EQ(probe.saves, 1);
    EXPECT_EQ(probe.applies, 0);

    s.step({Action::Confirm});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE) << "an empty slot is an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    // Another slot: the palette in use is untouched.
    const auto clearSlot = [](Probe& p, std::uint8_t preset) {
        Session t(p);
        t.ui().preset = preset;
        t.toRow(PaletteEditorRow::CLEAR);
        t.step({Action::Confirm});
        t.step({Action::MenuRight});
        t.step({Action::Confirm});
    };
    Probe other;
    other.palettes         = threeSlots();
    other.palettes.working = other.palettes.slots[0].shades;
    other.settings         = Settings{.shadeRamp = kPlayerRamp, .customPalette = true};
    clearSlot(other, 4);
    EXPECT_FALSE(other.palettes.slots[3].present);
    EXPECT_TRUE(other.settings.customPalette);

    // The palette in use is also saved in another slot: still saved, still in use.
    Probe twice;
    twice.palettes          = threeSlots();
    twice.palettes.slots[4] = twice.palettes.slots[6];
    twice.palettes.working  = twice.palettes.slots[6].shades;
    twice.settings          = Settings{.shadeRamp = kPlayerRamp, .customPalette = true};
    clearSlot(twice, 7);
    EXPECT_FALSE(twice.palettes.slots[6].present);
    EXPECT_TRUE(twice.settings.customPalette);
}

// A question's table: Left and Right pick an answer with a cue, nothing at an edge; B is no; both
// close with the screen-change cue and the cursor still on the row.
TEST(PaletteEditor, TheQuestionsTable) {
    Probe probe;
    probe.palettes = threeSlots();
    Session s(probe);
    s.toRow(PaletteEditorRow::CLEAR);
    s.step({Action::Confirm});
    ASSERT_EQ(s.ui().question, PaletteEditorQuestion::CLEAR);

    s.step({Action::MenuLeft});
    EXPECT_FALSE(s.ui().answerYes);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    s.step({Action::MenuRight});
    EXPECT_TRUE(s.ui().answerYes);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::MenuRight});
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    s.game.flow.timer1         = 3;
    s.game.screens.cursorVisible = false;
    s.step({Action::Back});
    EXPECT_EQ(s.ui().question, PaletteEditorQuestion::NONE);
    EXPECT_FALSE(s.ui().answerYes);
    EXPECT_TRUE(probe.palettes.slots[0].present) << "B is no";
    EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
    EXPECT_TRUE(s.game.screens.cursorVisible);
    EXPECT_EQ(s.game.flow.timer1, kBlinkFrames);
    EXPECT_EQ(s.ui().row, PaletteEditorRow::CLEAR);
}

// With no palettes behind the wiring the preset step, entering a value and the three actions are end
// stops; the walk and the way out still work.
TEST(PaletteEditor, ANullPaletteWiringIsInert) {
    Probe                     probe;
    const auto                wiring = probe.wiring();
    const CustomPaletteWiring none{};
    GameContext               game = openEditor(wiring, none);
    ASSERT_EQ(game.flow.gameState, GameState::PALETTE_EDITOR);

    const auto step = [&](Action a) {
        press(game, {a});
        kirpich::systems::paletteEditor(game, wiring, none);
    };
    step(Action::MenuRight);
    EXPECT_EQ(game.paletteEditor.preset, 1);
    EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);

    for (const PaletteEditorRow action :
         {PaletteEditorRow::LOAD, PaletteEditorRow::SAVE, PaletteEditorRow::CLEAR}) {
        step(Action::MenuDown);
        ASSERT_EQ(game.paletteEditor.row, action);
        step(Action::Confirm);
        EXPECT_EQ(game.paletteEditor.question, PaletteEditorQuestion::NONE);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }
    step(Action::MenuDown);
    step(Action::Confirm);
    EXPECT_FALSE(game.paletteEditor.entering);

    step(Action::Back);
    EXPECT_EQ(game.flow.gameState, GameState::PALETTE_SETTINGS);
}

// ── The picture ───────────────────────────────────────────────────────────────────────────────────

// A seeded working palette, saved: every sprite accounted for at its pixel, and the band's four
// rectangles in the working palette's colors.
TEST(PaletteEditor, ThePictureForASavedPalette) {
    CustomPalettes p = threeSlots();
    p.working        = p.slots[3].shades;
    const PaletteEditorState ui{.preset = 4};
    const auto               layers  = draw(ui, p);
    const auto               content = spritesOf(layers);

    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].key.value, "editor-backdrop");
    EXPECT_EQ(layers[1].key.value, "editor-content");
    EXPECT_EQ(layers[1].z, layout::kContentZ);

    std::size_t expected = 0;
    const auto  run      = [&](std::string_view text, int x, int y, bool dim = false) {
        const auto want = dim ? kirpich::render::DimGlyphs(text, x, y, 8, kAtlas, kPlayerRamp)
                              : kirpich::render::Glyphs(text, x, y, 8, kAtlas, kPlayerRamp);
        expectRun(content, want, text);
        expected += want.size();
    };

    const auto up = named(content, "editor-up");
    ASSERT_TRUE(up);
    EXPECT_EQ(up->x, 80);
    EXPECT_EQ(up->y, 8);
    EXPECT_EQ(up->rotation, retropp::Rotation::Rot270);
    ++expected;

    run("custom palette", 24, 16);
    run("preset", 24, 32);
    run("04", 120, 32);
    // Slot 4 holds the working palette, so loading or saving it would change nothing.
    run("load preset", 24, 40, /*dim=*/true);
    run("save preset", 24, 48, /*dim=*/true);
    run("clear preset", 24, 56);

    const auto left  = named(content, "preset-left");
    const auto right = named(content, "preset-right");
    ASSERT_TRUE(left && right);
    EXPECT_EQ(left->x, 104);
    EXPECT_TRUE(left->flipX);
    EXPECT_EQ(right->x, 152);
    EXPECT_EQ(left->y, 32);
    expected += 2;

    for (std::size_t shade = 0; shade < 4; ++shade) {
        const int y = 72 + 16 * static_cast<int>(shade);
        run(std::to_string(shade + 1), 8, y);
        for (std::size_t c = 0; c < kChannels.size(); ++c) {
            run(padded(kirpich::channelValue(p.working, shade, kChannels[c]), 3),
                24 + 32 * static_cast<int>(c), y);
        }
    }
    EXPECT_EQ(content.size(), expected) << "nothing else is drawn";
    EXPECT_FALSE(named(content, glyphKey(0, 136))) << "no hint while nothing is entered";

    const auto& band = layers[1].regions;
    ASSERT_EQ(band.size(), 4u);
    const std::array colors{p.working.darkest, p.working.dark, p.working.light, p.working.lightest};
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(band[i].key.value, "editor-shade-" + std::to_string(i));
        ASSERT_EQ(band[i].effects.size(), 1u);
        EXPECT_EQ(band[i].effects[0].kind, retropp::ScreenSpaceEffectKind::ColorFill);
        EXPECT_EQ(band[i].effects[0].fill.r, colors[i].r);
        EXPECT_EQ(band[i].effects[0].fill.g, colors[i].g);
        EXPECT_EQ(band[i].effects[0].fill.b, colors[i].b);
        EXPECT_EQ(band[i].effects[0].fill.a, 255);
        const auto& pts = band[i].shape.points;
        EXPECT_FLOAT_EQ(pts[0].x, 120.0f);
        EXPECT_FLOAT_EQ(pts[0].y, 72.0f + 16.0f * static_cast<float>(i));
        EXPECT_FLOAT_EQ(pts[2].x - pts[0].x, 16.0f);
        EXPECT_FLOAT_EQ(pts[2].y - pts[0].y, 16.0f);
    }
}

// The asterisk stands in the cell after the heading while the working palette has unsaved changes -
// saved in no slot and not the untouched grayscale - drawn from the extras sheet through the font
// object palette; the heading does not move for it.
TEST(PaletteEditor, TheAsteriskMarksUnsavedChanges) {
    const CustomPalettes untouched;
    EXPECT_FALSE(kirpich::hasUnsavedChanges(untouched));
    EXPECT_FALSE(named(spritesOf(draw(PaletteEditorState{}, untouched)), glyphKey(136, 16)))
        << "a palette never edited has nothing to save";

    CustomPalettes edited;
    kirpich::setChannelValue(edited.working, 0, ColorChannel::RED, 1);
    EXPECT_TRUE(kirpich::hasUnsavedChanges(edited));
    EXPECT_TRUE(named(spritesOf(draw(PaletteEditorState{}, edited)), glyphKey(136, 16)))
        << "one step away from the grayscale";


    CustomPalettes p = threeSlots();
    p.working        = rampFrom(99);
    const auto unsaved = spritesOf(draw(PaletteEditorState{}, p));
    const auto star    = named(unsaved, glyphKey(136, 16));
    ASSERT_TRUE(star);
    EXPECT_EQ(star->atlas, kAtlas.glyphExtras);
    EXPECT_EQ(star->tile, kirpich::render::kAsteriskCell);
    EXPECT_EQ(star->palette, kAtlas.palettes[kPlayerRamp].fontSprite);
    EXPECT_TRUE(named(unsaved, glyphKey(24, 16))) << "the heading starts where it always does";

    p.working        = p.slots[0].shades;
    const auto saved = spritesOf(draw(PaletteEditorState{}, p));
    EXPECT_FALSE(named(saved, glyphKey(136, 16)));
    EXPECT_TRUE(named(saved, glyphKey(24, 16)));
}

// The cursor: before the label on the four upper rows, before the channel's value on a shade row,
// and nowhere while the blink is off or a value is entered.
TEST(PaletteEditor, TheCursor) {
    const CustomPalettes p;
    const auto cursorAt = [&p](const PaletteEditorState& ui, int x, int y, bool blinkOn = true) {
        return named(spritesOf(draw(ui, p, blinkOn)), glyphKey(x, y)).has_value();
    };
    EXPECT_TRUE(cursorAt(PaletteEditorState{.row = PaletteEditorRow::PRESET}, 8, 32));
    EXPECT_TRUE(cursorAt(PaletteEditorState{.row = PaletteEditorRow::LOAD}, 8, 40));
    EXPECT_TRUE(cursorAt(PaletteEditorState{.row = PaletteEditorRow::SAVE}, 8, 48));
    EXPECT_TRUE(cursorAt(PaletteEditorState{.row = PaletteEditorRow::CLEAR}, 8, 56));
    EXPECT_TRUE(cursorAt(
        PaletteEditorState{.row = PaletteEditorRow::SHADE_2, .channel = ColorChannel::GREEN}, 48,
        88));
    EXPECT_TRUE(cursorAt(
        PaletteEditorState{.row = PaletteEditorRow::SHADE_4, .channel = ColorChannel::BLUE}, 80,
        120));
    EXPECT_FALSE(cursorAt(PaletteEditorState{.row = PaletteEditorRow::LOAD}, 8, 40, false));

    const auto content = spritesOf(draw(
        PaletteEditorState{.row = PaletteEditorRow::SHADE_2, .channel = ColorChannel::GREEN,
                           .entering = true},
        p, true));
    EXPECT_FALSE(named(content, glyphKey(48, 88))) << "no cursor while a value is entered";
}

// An entered value: its three digits selected and every other value inked; an arrow either side only
// where the place can move; an arrow over and under the digit being stepped only where the value can
// move; the hint at the bottom.
TEST(PaletteEditor, TheEnteredValuesSelector) {
    CustomPalettes p;
    kirpich::setChannelValue(p.working, 1, ColorChannel::GREEN, 128);
    const PaletteEditorState ui{.row      = PaletteEditorRow::SHADE_2,
                                .channel  = ColorChannel::GREEN,
                                .entering = true,
                                .place    = DigitPlace::TENS};
    const auto content = spritesOf(draw(ui, p));

    expectRun(content,
              kirpich::render::SelectedGlyphs("128", 56, 88, 8, kAtlas, kPlayerRamp),
              "the entered value");
    expectRun(content,
              kirpich::render::Glyphs(padded(kirpich::channelValue(p.working, 1, ColorChannel::RED), 3),
                                      24, 88, 8, kAtlas, kPlayerRamp),
              "the red value beside it");

    const auto expectArrow = [&content](std::string_view key, int x, int y, bool flip,
                                        retropp::Rotation turn) {
        const auto a = named(content, key);
        ASSERT_TRUE(a) << key;
        EXPECT_EQ(a->x, x) << key;
        EXPECT_EQ(a->y, y) << key;
        EXPECT_EQ(a->flipX, flip) << key;
        EXPECT_EQ(a->rotation, turn) << key;
        EXPECT_EQ(a->z, layout::kArrowZ) << key;
    };
    expectArrow("value-left", 48, 88, true, retropp::Rotation::None);
    expectArrow("value-right", 80, 88, false, retropp::Rotation::None);
    expectArrow("digit-up", 64, 80, false, retropp::Rotation::Rot270);
    expectArrow("digit-down", 64, 96, false, retropp::Rotation::Rot90);
    expectRun(content, kirpich::render::Glyphs("a confirms b cancels", 0, 136, 8, kAtlas, kPlayerRamp),
              "the hint");

    const auto at = [&p](DigitPlace place, std::uint8_t value) {
        CustomPalettes q = p;
        kirpich::setChannelValue(q.working, 1, ColorChannel::GREEN, value);
        PaletteEditorState u{.row      = PaletteEditorRow::SHADE_2,
                             .channel  = ColorChannel::GREEN,
                             .entering = true,
                             .place    = place};
        const auto c = spritesOf(draw(u, q));
        return std::array{named(c, "value-left").has_value(), named(c, "value-right").has_value(),
                          named(c, "digit-up").has_value(), named(c, "digit-down").has_value()};
    };
    EXPECT_EQ(at(DigitPlace::HUNDREDS, 128), (std::array{false, true, true, true}));
    EXPECT_EQ(at(DigitPlace::ONES, 128), (std::array{true, false, true, true}));
    EXPECT_EQ(at(DigitPlace::TENS, 255), (std::array{true, true, false, true}));
    EXPECT_EQ(at(DigitPlace::TENS, 0), (std::array{true, true, true, false}));
}

// The preset's number is dimmed exactly while its slot is empty.
TEST(PaletteEditor, AnEmptyPresetsNumberIsDimmed) {
    const CustomPalettes p     = threeSlots();
    const auto           digit = [&p](std::uint8_t preset) {
        return named(spritesOf(draw(PaletteEditorState{.preset = preset}, p)), glyphKey(120, 32));
    };
    const auto& set = kAtlas.palettes[kPlayerRamp];
    for (std::uint8_t preset = 1; preset <= 9; ++preset) {
        const auto first = digit(preset);
        ASSERT_TRUE(first) << +preset;
        const bool present = p.slots[preset - 1].present;
        EXPECT_EQ(first->palette, present ? set.fontSprite : set.fontSpriteDim) << +preset;
    }
}

// Each action's label is dimmed exactly while the action can do nothing.
TEST(PaletteEditor, ActionLabelsDimWhileTheyCanDoNothing) {
    const auto dimmed = [](const CustomPalettes& p, std::uint8_t preset, int y) {
        const auto first = named(spritesOf(draw(PaletteEditorState{.preset = preset}, p)),
                                 glyphKey(24, y));
        return first && first->palette == kAtlas.palettes[kPlayerRamp].fontSpriteDim;
    };
    CustomPalettes p = threeSlots();

    p.working = p.slots[0].shades;
    EXPECT_TRUE(dimmed(p, 1, 40)) << "load: the slot holds the working palette";
    EXPECT_FALSE(dimmed(p, 4, 40)) << "load: a different slot";
    EXPECT_TRUE(dimmed(p, 2, 40)) << "load: an empty slot has nothing to load";
    p.working = kirpich::render::freshCustomPalette();
    EXPECT_TRUE(dimmed(p, 2, 40)) << "load: an empty slot onto the grayscale";

    p.working = p.slots[0].shades;
    EXPECT_TRUE(dimmed(p, 1, 48)) << "save: the slot holds it already";
    EXPECT_FALSE(dimmed(p, 4, 48)) << "save: over a different palette";
    EXPECT_FALSE(dimmed(p, 2, 48)) << "save: into an empty slot";

    EXPECT_TRUE(dimmed(p, 2, 56)) << "clear: an empty slot";
    EXPECT_FALSE(dimmed(p, 1, 56)) << "clear: the slot in use";
    EXPECT_FALSE(dimmed(p, 7, 56)) << "clear: another present slot";
}

// A question takes the screen: its layer in place of the editor's, the title on the heading row, the
// two lines and the answers on the confirm's places, the preset as two digits, the cursor on the
// chosen answer.
TEST(PaletteEditor, EachQuestionTakesTheScreen) {
    const CustomPalettes p = threeSlots();
    struct Case {
        PaletteEditorQuestion q;
        std::string_view      title, first, second;
    };
    const std::array<Case, 3> cases{{
        {PaletteEditorQuestion::LOAD_UNSAVED, "load preset", "your palette is", "not saved"},
        {PaletteEditorQuestion::SAVE_OVER, "save preset", "replace", "preset 07"},
        {PaletteEditorQuestion::CLEAR, "clear preset", "empty", "preset 07"},
    }};
    for (const Case& c : cases) {
        const PaletteEditorState ui{.row = PaletteEditorRow::SAVE, .preset = 7, .question = c.q,
                                    .answerYes = true};
        const auto layers = draw(ui, p, true);
        ASSERT_EQ(layers.size(), 2u);
        EXPECT_EQ(layers[1].key.value, "editor-question");
        const auto content = spritesOf(layers);

        const auto centered = [](std::string_view text) {
            return text.size() >= 20 ? 0 : static_cast<int>((20 - text.size()) / 2) * 8;
        };
        expectRun(content, kirpich::render::Glyphs(c.title, centered(c.title), 16, 8, kAtlas, kPlayerRamp),
                  c.title);
        expectRun(content, kirpich::render::Glyphs(c.first, centered(c.first), 40, 8, kAtlas, kPlayerRamp),
                  c.first);
        expectRun(content,
                  kirpich::render::Glyphs(c.second, centered(c.second), 56, 8, kAtlas, kPlayerRamp),
                  c.second);
        expectRun(content, kirpich::render::Glyphs("no", 48, 88, 8, kAtlas, kPlayerRamp), "no");
        expectRun(content, kirpich::render::Glyphs("yes", 96, 88, 8, kAtlas, kPlayerRamp), "yes");
        EXPECT_TRUE(named(content, glyphKey(80, 88))) << "the cursor before yes";
        EXPECT_FALSE(named(content, glyphKey(32, 88))) << "and not before no";
    }
}

// The editor draws through the built-in ramp underneath, even while the custom palette is in use.
TEST(PaletteEditor, TheEditorDrawsThroughTheRampUnderneath) {
    const CustomPalettes p = threeSlots();
    const Settings       s{.shadeRamp = kPlayerRamp, .customPalette = true};
    const auto           layers = draw(PaletteEditorState{.preset = 2}, p, true, s);

    const auto& set = kAtlas.palettes[kPlayerRamp];
    for (const retropp::Sprite& sprite : spritesOf(layers)) {
        if (sprite.key.value.starts_with("glyph-")) {
            EXPECT_TRUE(sprite.palette == set.fontSprite || sprite.palette == set.fontSpriteDim ||
                        sprite.palette == set.fontSpriteSelected)
                << sprite.key.value;
        } else {
            EXPECT_EQ(sprite.palette, set.sprite0) << sprite.key.value;
        }
    }
    const auto& backdrop = std::get<retropp::TileContent>(layers[0].content).cells;
    ASSERT_FALSE(backdrop.empty());
    EXPECT_EQ(backdrop[0].palette, set.content);
}

// No two objects in a frame share a name, and every sprite is on the screen - swept over the idle
// screen, a value entered at each place, and each question.
TEST(PaletteEditor, EveryFrameNamesEachObjectOnceAndStaysOnScreen) {
    CustomPalettes p = threeSlots();
    std::vector<PaletteEditorState> states{PaletteEditorState{.preset = 4}};
    for (const DigitPlace place : {DigitPlace::HUNDREDS, DigitPlace::TENS, DigitPlace::ONES}) {
        for (const PaletteEditorRow row : {PaletteEditorRow::SHADE_1, PaletteEditorRow::SHADE_4}) {
            for (const ColorChannel channel : kChannels) {
                states.push_back(PaletteEditorState{
                    .row = row, .channel = channel, .entering = true, .place = place});
            }
        }
    }
    for (const PaletteEditorQuestion q : {PaletteEditorQuestion::LOAD_UNSAVED,
                                          PaletteEditorQuestion::SAVE_OVER,
                                          PaletteEditorQuestion::CLEAR}) {
        states.push_back(PaletteEditorState{.preset = 4, .question = q});
    }

    for (const PaletteEditorState& ui : states) {
        const auto            layers = draw(ui, p, true);
        std::set<std::string> keys;
        for (const retropp::Sprite& s : spritesOf(layers)) {
            EXPECT_TRUE(keys.insert(s.key.value).second) << "two objects named " << s.key.value;
            EXPECT_GE(s.x, 0);
            EXPECT_GE(s.y, 0);
            EXPECT_LE(s.x + 8, 160) << s.key.value;
            EXPECT_LE(s.y + 8, 144) << s.key.value;
        }
        for (const retropp::Region& r : layers[1].regions) {
            EXPECT_TRUE(keys.insert(r.key.value).second) << "two objects named " << r.key.value;
        }
    }
}

// ── The components ────────────────────────────────────────────────────────────────────────────────

// Arrow: the selector tile in the copyright-and-title art, as is to point right, flipped to point
// left, turned to point up or down, at the pixel and depth given, through the ramp's object palette.
TEST(PaletteEditor, AnArrowPointsEachWay) {
    const kirpich::render::ResolvedTile art = kirpich::render::resolveSpriteTile(
        0x58, kirpich::TileSheet::COPYRIGHT_TITLE, false, kAtlas, kPlayerRamp);
    struct Case {
        ArrowDirection    direction;
        bool              flip;
        retropp::Rotation turn;
    };
    for (const Case c : {Case{ArrowDirection::RIGHT, false, retropp::Rotation::None},
                         Case{ArrowDirection::LEFT, true, retropp::Rotation::None},
                         Case{ArrowDirection::UP, false, retropp::Rotation::Rot270},
                         Case{ArrowDirection::DOWN, false, retropp::Rotation::Rot90}}) {
        const auto sprites =
            kirpich::render::Arrow("a", 12, 34, c.direction, 56, kAtlas, kPlayerRamp);
        ASSERT_EQ(sprites.size(), 1u);
        const retropp::Sprite& s = sprites[0];
        EXPECT_EQ(s.key.value, "a");
        EXPECT_EQ(s.x, 12);
        EXPECT_EQ(s.y, 34);
        EXPECT_EQ(s.z, 56);
        EXPECT_EQ(s.atlas, art.atlas);
        EXPECT_EQ(s.tile, art.cell);
        EXPECT_EQ(s.palette, art.palette);
        EXPECT_EQ(s.flipX, c.flip);
        EXPECT_FALSE(s.flipY);
        EXPECT_EQ(s.rotation, c.turn);
    }
    EXPECT_EQ(kirpich::kSelectorTile, 0x58);
}

// PageArrows: the four screens that draw page arrows draw exactly what PageArrows draws for their
// name and conditions - the up arrow at (80, 8) turned up, the down arrow at (80, 128) turned down,
// both above the object buffer's sprites.
TEST(PaletteEditor, EveryScreensPageArrowsArePageArrows) {
    kirpich::ScreenUiState ui;
    ui.settingsRow    = SettingsRow::GHOST_PIECE;  // the middle page
    ui.carouselOption = 1;
    ui.listTop        = 1;
    ui.listCount      = static_cast<std::uint8_t>(1 + kirpich::systems::kListRows + 1);
    ui.statsPage      = 1;
    ui.statsPageCount = 3;

    const auto same = [](const kirpich::render::Sprites& got, const kirpich::render::Sprites& want,
                         std::string_view what) {
        ASSERT_EQ(got.size(), want.size()) << what;
        for (std::size_t i = 0; i < got.size(); ++i) {
            EXPECT_EQ(got[i].key.value, want[i].key.value) << what;
            EXPECT_EQ(got[i].x, want[i].x) << what;
            EXPECT_EQ(got[i].y, want[i].y) << what;
            EXPECT_EQ(got[i].z, want[i].z) << what;
            EXPECT_EQ(got[i].palette, want[i].palette) << what;
            EXPECT_EQ(got[i].rotation, want[i].rotation) << what;
        }
    };
    using kirpich::render::PageArrows;
    same(kirpich::render::settingsPageArrows(ui, kPlayerRamp, kAtlas),
         PageArrows("settings-page", true, true, kAtlas, kPlayerRamp), "settings");
    same(kirpich::render::carouselArrows(ui, kPlayerRamp, kAtlas, 3),
         PageArrows("carousel", true, true, kAtlas, kPlayerRamp), "carousel");
    same(kirpich::render::listArrows(ui, kPlayerRamp, kAtlas),
         PageArrows("list", true, true, kAtlas, kPlayerRamp), "list");
    same(kirpich::render::statsPageArrows(ui, kPlayerRamp, kAtlas),
         PageArrows("stats-page", true, true, kAtlas, kPlayerRamp), "stats");

    const auto both = PageArrows("x", true, true, kAtlas, kPlayerRamp);
    ASSERT_EQ(both.size(), 2u);
    EXPECT_EQ(both[0].key.value, "x-up");
    EXPECT_EQ(both[0].x, 80);
    EXPECT_EQ(both[0].y, 8);
    EXPECT_EQ(both[0].rotation, retropp::Rotation::Rot270);
    EXPECT_EQ(both[1].key.value, "x-down");
    EXPECT_EQ(both[1].x, 80);
    EXPECT_EQ(both[1].y, 128);
    EXPECT_EQ(both[1].rotation, retropp::Rotation::Rot90);
    EXPECT_EQ(both[0].z, 100);
    EXPECT_TRUE(PageArrows("x", false, false, kAtlas, kPlayerRamp).empty());
}

// The ninth palette inks in the dark shade; the asterisk's plane is the font's convention, 0 for ink.
TEST(PaletteEditor, TheSelectedFontPaletteAndTheAsterisksPlane) {
    const ShadeRamp shades = rampFrom(10);
    const auto      colors = kirpich::render::rampPaletteColors(shades);
    EXPECT_EQ(colors.fontSpriteSelected,
              (std::array{shades.dark, kirpich::render::kShadeTransparent}));

    const auto plane = kirpich::render::asteriskIndices();
    ASSERT_EQ(plane.size(), 64u);
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t col = 0; col < 8; ++col) {
            const bool ink = kirpich::render::kAsteriskArt[row][col] == '#';
            EXPECT_EQ(plane[row * 8 + col], ink ? 0 : 1) << row << "," << col;
        }
    }
}

// SelectedGlyphs draws through the selected font palette under the names Glyphs gives the same run;
// an asterisk draws from the extras sheet through each form's own palette.
TEST(PaletteEditor, SelectedGlyphsAndTheAsterisk) {
    const auto& set      = kAtlas.palettes[kPlayerRamp];
    const auto  inked    = kirpich::render::Glyphs("12", 0, 0, 8, kAtlas, kPlayerRamp);
    const auto  selected = kirpich::render::SelectedGlyphs("12", 0, 0, 8, kAtlas, kPlayerRamp);
    ASSERT_EQ(selected.size(), inked.size());
    for (std::size_t i = 0; i < selected.size(); ++i) {
        EXPECT_EQ(selected[i].key.value, inked[i].key.value);
        EXPECT_EQ(selected[i].tile, inked[i].tile);
        EXPECT_EQ(selected[i].palette, set.fontSpriteSelected);
    }

    const auto star = [](const kirpich::render::Sprites& run) {
        EXPECT_EQ(run.size(), 1u);
        return run.at(0);
    };
    const auto ink = star(kirpich::render::Glyphs("*", 0, 0, 8, kAtlas, kPlayerRamp));
    const auto dim = star(kirpich::render::DimGlyphs("*", 0, 0, 8, kAtlas, kPlayerRamp));
    const auto sel = star(kirpich::render::SelectedGlyphs("*", 0, 0, 8, kAtlas, kPlayerRamp));
    for (const retropp::Sprite& s : {ink, dim, sel}) {
        EXPECT_EQ(s.atlas, kAtlas.glyphExtras);
        EXPECT_EQ(s.tile, kirpich::render::kAsteriskCell);
    }
    EXPECT_EQ(ink.palette, set.fontSprite);
    EXPECT_EQ(dim.palette, set.fontSpriteDim);
    EXPECT_EQ(sel.palette, set.fontSpriteSelected);
}
