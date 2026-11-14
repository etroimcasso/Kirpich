// The rotation screen - behavioral tests over its logic (src/render/screens/palette_rotation/
// rotation_screen.h), its picture (screen.h beside it and the grid components there), and the two
// shared components it adds (src/render/components/color_swatch.h, level_selector.h).
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
#include "render/components/color_swatch.h"
#include "render/components/glyphs.h"
#include "render/components/level_selector.h"
#include "render/components/palette_preview.h"
#include "render/components/preview_backdrop.h"
#include "render/palettes.h"
#include "render/rotation_presets.h"
#include "render/screens/palette_rotation/layout.h"
#include "render/screens/palette_rotation/rotation_screen.h"
#include "render/screens/palette_rotation/screen.h"
#include "render/tile_atlas.h"
#include "retropp/input.h"
#include "state/custom_rotations.h"
#include "state/screen_ui_state.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/palette_settings_screen.h"
#include "systems/rotation_walk.h"
#include "systems/settings_screen.h"

namespace {

namespace layout = kirpich::render::palette_rotation;

using kirpich::Action;
using kirpich::CustomRotations;
using kirpich::CustomRotationSlot;
using kirpich::GameState;
using kirpich::PaletteRotation;
using kirpich::RotationProgression;
using kirpich::RotationScreenRow;
using kirpich::RotationScreenState;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::SquareSfxId;
using kirpich::render::kCustomRamp;
using kirpich::render::kRotationRamp;
using kirpich::render::kShadeRampCount;
using kirpich::render::RampPalettes;
using kirpich::render::ShadeRamp;
using kirpich::render::TileAtlas;
using kirpich::systems::CustomPaletteWiring;
using kirpich::systems::GameContext;
using kirpich::systems::RotationWiring;
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
    fill(atlas.custom, kCustomRamp);
    fill(atlas.rotation, kRotationRamp);
    // The two live sets hold colors unlike any ramp's and unlike each other's.
    atlas.customShades   = ShadeRamp{.darkest  = {.r = 1, .g = 2, .b = 3},
                                     .dark     = {.r = 4, .g = 5, .b = 6},
                                     .light    = {.r = 7, .g = 8, .b = 9},
                                     .lightest = {.r = 10, .g = 11, .b = 12}};
    atlas.rotationShades = ShadeRamp{.darkest  = {.r = 201, .g = 202, .b = 203},
                                     .dark     = {.r = 204, .g = 205, .b = 206},
                                     .light    = {.r = 207, .g = 208, .b = 209},
                                     .lightest = {.r = 210, .g = 211, .b = 212}};
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

// Slots 1, 4 and 7 saved, each ten distinct palettes; the working rotation the fresh one.
CustomRotations threeSlots() {
    CustomRotations r;
    r.slots[0] = CustomRotationSlot{.present = true, .progression = distinct(10)};
    r.slots[3] = CustomRotationSlot{.present = true, .progression = distinct(40)};
    r.slots[6] = CustomRotationSlot{.present = true, .progression = distinct(70)};
    return r;
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
    CustomRotations rotations = threeSlots();
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

// The screen opened the way a player opens it: the settings screen from the title, the Palette screen
// from its row, and Up.
GameContext openScreen(const SettingsWiring& wiring) {
    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, wiring);
    game.screens.settingsRow = SettingsRow::PALETTE;
    press(game, {Action::Confirm});
    kirpich::systems::settingsScreen(game, wiring);
    press(game, {Action::MenuUp});
    kirpich::systems::paletteSettingsScreen(game, wiring, CustomPaletteWiring{});
    return game;
}

struct Session {
    Probe&         probe;
    SettingsWiring wiring;
    RotationWiring rotations;
    GameContext    game;

    explicit Session(Probe& p, bool wired = true)
        : probe(p),
          wiring(p.wiring()),
          rotations(wired ? p.rotation() : RotationWiring{}),
          game(openScreen(wiring)) {}

    void step(std::initializer_list<Action> as) {
        press(game, as);
        kirpich::systems::rotationScreen(game, wiring, rotations);
    }
    void holdFor(std::initializer_list<Action> as) {
        hold(game, as);
        kirpich::systems::rotationScreen(game, wiring, rotations);
    }
    RotationScreenState& ui() { return game.rotationScreen; }
    SquareSfxId          sound() const { return game.audioCues.square; }
    PaletteRotation      mode() const { return probe.settings.paletteRotation; }
};

kirpich::render::Layers draw(const RotationScreenState& ui, const Settings& s,
                             const CustomRotations& rotations, bool blinkOn = false) {
    return kirpich::render::PaletteRotationScreen(ui, s, rotations, blinkOn, kAtlas);
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

const retropp::Region* region(const kirpich::render::Layers& layers, std::string_view key) {
    for (const retropp::Region& r : layers[1].regions) {
        if (r.key.value == key) return &r;
    }
    return nullptr;
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
    }
}

std::array<retropp::Rgba8, 4> colorsOf(const ShadeRamp& r) {
    return {r.darkest, r.dark, r.light, r.lightest};
}

std::set<retropp::PaletteId> handlesOf(const RampPalettes& set) {
    return {set.font,          set.content,   set.fontDim,
            set.fontSprite,    set.sprite0,   set.sprite1,
            set.fontSpriteDim, set.spriteDim, set.fontSpriteSelected};
}

// Every stop of the walk over threeSlots(), and the off-walk `C`, as settings and the rotations behind
// them. The value each reads is beside it.
struct Stop {
    Settings        settings;
    CustomRotations rotations;
    std::string     value;
};

std::vector<Stop> everyStop() {
    std::vector<Stop> out;
    const auto        add = [&out](PaletteRotation m, std::string value,
                            std::optional<std::size_t> savedSlot = std::nullopt) {
        CustomRotations r = threeSlots();
        if (savedSlot) {
            r.working = r.slots[*savedSlot - 1].progression;
        } else if (m == PaletteRotation::CUSTOM) {
            r.working = distinct(99);
        }
        out.push_back(Stop{Settings{.shadeRamp = kPlayerRamp, .paletteRotation = m}, r, value});
    };
    add(PaletteRotation::OFF, "off");
    add(PaletteRotation::HOTTER, "hotter");
    add(PaletteRotation::COLDER, "colder");
    add(PaletteRotation::RAINBOW, "rainbow");
    add(PaletteRotation::CUSTOM, "C01", 1);
    add(PaletteRotation::CUSTOM, "C04", 4);
    add(PaletteRotation::CUSTOM, "C07", 7);
    add(PaletteRotation::CUSTOM, "C");
    return out;
}

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

// (1) Opening shows the cursor, arms the blink, resets every part of the state - whatever the last
// visit left - and enters the screen.
TEST(RotationScreen, OpeningResetsTheStateAndEntersTheScreen) {
    GameContext game;
    game.flow.gameState        = GameState::PALETTE_SETTINGS;
    game.flow.timer1           = 3;
    game.screens.cursorVisible = false;
    game.rotationScreen        = RotationScreenState{
               .row = RotationScreenRow::EDITOR, .previewing = true, .previewLevel = 8};

    kirpich::systems::openRotationScreen(game);

    EXPECT_EQ(game.flow.gameState, GameState::PALETTE_ROTATION);
    EXPECT_EQ(game.rotationScreen, RotationScreenState{});
    EXPECT_EQ(game.rotationScreen.row, RotationScreenRow::SELECTOR);
    EXPECT_FALSE(game.rotationScreen.previewing);
    EXPECT_EQ(game.rotationScreen.previewLevel, 0);
    EXPECT_TRUE(game.screens.cursorVisible);
    EXPECT_EQ(game.flow.timer1, kBlinkFrames);
}

// (2) The cursor walks the two rows, with an end stop above the editor item.
TEST(RotationScreen, TheCursorWalksTwoRowsWithItsEndStop) {
    Probe   probe;
    Session s(probe);
    ASSERT_EQ(s.ui().row, RotationScreenRow::SELECTOR);

    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().row, RotationScreenRow::EDITOR);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);

    s.step({Action::MenuUp});
    EXPECT_EQ(s.ui().row, RotationScreenRow::EDITOR) << "an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE) << "an end stop cues nothing";
    EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_ROTATION);

    s.step({Action::MenuDown});
    EXPECT_EQ(s.ui().row, RotationScreenRow::SELECTOR);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
}

// (3) Down on the selector and B both return to the Palette screen, with the screen-change cue, the
// cursor shown and the blink armed. The settings are untouched.
TEST(RotationScreen, DownOnTheSelectorAndBReturnToThePaletteScreen) {
    for (const Action leave : {Action::MenuDown, Action::Back}) {
        for (const RotationScreenRow row :
             {RotationScreenRow::SELECTOR, RotationScreenRow::EDITOR}) {
            if (leave == Action::MenuDown && row == RotationScreenRow::EDITOR) continue;
            Probe   probe;
            Session s(probe);
            s.ui().row                   = row;
            s.game.flow.timer1           = 3;
            s.game.screens.cursorVisible = false;
            const Settings before        = probe.settings;

            s.step({leave});
            EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_SETTINGS)
                << static_cast<int>(leave);
            EXPECT_EQ(s.sound(), SquareSfxId::CHANGE_SCREEN);
            EXPECT_TRUE(s.game.screens.cursorVisible);
            EXPECT_EQ(s.game.flow.timer1, kBlinkFrames);
            EXPECT_EQ(probe.settings, before);
            EXPECT_EQ(probe.settingsSaves, 0);
        }
    }
}

// (4) The selector steps every stop both ways, with an end stop at each end: from `off`, Right lands
// on hotter, colder, rainbow, then slot 1, 4 and 7, then stops; Left walks back to `off` and stops.
// Every step cues; the setting is written only where the mode byte changes.
TEST(RotationScreen, TheSelectorStepsEveryStopBothWays) {
    Probe   probe;
    Session s(probe);
    using kirpich::systems::RotationChoice;
    const std::vector<RotationChoice> walk{
        {.mode = PaletteRotation::OFF},     {.mode = PaletteRotation::HOTTER},
        {.mode = PaletteRotation::COLDER},  {.mode = PaletteRotation::RAINBOW},
        {.mode = PaletteRotation::CUSTOM, .slot = 1}, {.mode = PaletteRotation::CUSTOM, .slot = 4},
        {.mode = PaletteRotation::CUSTOM, .slot = 7},
    };
    const auto at = [&] {
        return kirpich::systems::rotationChoiceOf(probe.settings, &probe.rotations);
    };
    ASSERT_EQ(at(), walk[0]);

    int expectedSaves = 0;
    for (std::size_t i = 1; i < walk.size(); ++i) {
        const PaletteRotation before = s.mode();
        s.step({Action::MenuRight});
        EXPECT_EQ(at(), walk[i]) << "step " << i;
        EXPECT_EQ(s.sound(), SquareSfxId::TINK) << "step " << i;
        if (s.mode() != before) ++expectedSaves;
        EXPECT_EQ(probe.settingsSaves, expectedSaves) << "step " << i;
    }
    s.step({Action::MenuRight});
    EXPECT_EQ(at(), walk.back()) << "an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    for (std::size_t i = walk.size() - 1; i-- > 0;) {
        s.step({Action::MenuLeft});
        EXPECT_EQ(at(), walk[i]) << "back to " << i;
        EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    }
    s.step({Action::MenuLeft});
    EXPECT_EQ(s.mode(), PaletteRotation::OFF) << "an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
}

// (5) Stepping onto a slot copies it into the working rotation, writes the rotations, then stores
// CUSTOM. Between two slots the setting does not change, so it is not written.
TEST(RotationScreen, SteppingOntoASlotCopiesItSavesThenStoresCustom) {
    Probe probe;
    probe.settings.paletteRotation = PaletteRotation::RAINBOW;
    Session s(probe);

    s.step({Action::MenuRight});
    EXPECT_EQ(probe.rotations.working, probe.rotations.slots[0].progression);
    EXPECT_EQ(probe.rotationSaves, 1);
    EXPECT_EQ(s.mode(), PaletteRotation::CUSTOM);
    EXPECT_EQ(probe.settingsSaves, 1) << "rainbow to custom changes the mode";

    s.step({Action::MenuRight});
    EXPECT_EQ(probe.rotations.working, probe.rotations.slots[3].progression);
    EXPECT_EQ(probe.rotationSaves, 2);
    EXPECT_EQ(s.mode(), PaletteRotation::CUSTOM);
    EXPECT_EQ(probe.settingsSaves, 1) << "slot to slot changes no setting";
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);

    // Stepping off the slots back onto a preset writes the mode and leaves the working rotation alone.
    s.step({Action::MenuLeft});
    s.step({Action::MenuLeft});
    EXPECT_EQ(s.mode(), PaletteRotation::RAINBOW);
    EXPECT_EQ(probe.rotations.working, probe.rotations.slots[0].progression);
    EXPECT_EQ(probe.rotationSaves, 3) << "the step from 4 to 1 copied slot 1 again";
}

// (6) A working rotation saved in no slot reads `C`, and steps back onto the walk: Left to rainbow,
// Right to the first slot.
TEST(RotationScreen, AnUnsavedWorkingRotationReadsCAndStepsBackOntoTheWalk) {
    for (const auto& [press, expected] :
         {std::pair{Action::MenuLeft, PaletteRotation::RAINBOW},
          std::pair{Action::MenuRight, PaletteRotation::CUSTOM}}) {
        Probe probe;
        probe.settings.paletteRotation = PaletteRotation::CUSTOM;
        probe.rotations.working        = distinct(99);
        Session s(probe);

        const auto content = spritesOf(draw(s.ui(), probe.settings, probe.rotations));
        expectSprites(content,
                      kirpich::render::Glyphs("C", layout::kSelectorValueX, layout::kSelectorY, 8,
                                              kAtlas, kPlayerRamp),
                      "C");
        EXPECT_FALSE(named(content, glyphKey(layout::kSelectorValueX + 8, layout::kSelectorY)));

        s.step({press});
        EXPECT_EQ(s.mode(), expected);
        if (press == Action::MenuRight) {
            EXPECT_EQ(probe.rotations.working, probe.rotations.slots[0].progression);
        }
    }
}

// (7) The value reads the mode's name, or `C` and the saved slot as two digits, or `C` alone.
TEST(RotationScreen, TheValueReadsTheModesNameOrTheSlot) {
    for (const Stop& stop : everyStop()) {
        const auto content = spritesOf(draw(RotationScreenState{}, stop.settings, stop.rotations));
        expectSprites(content,
                      kirpich::render::Glyphs(stop.value, layout::kSelectorValueX, layout::kSelectorY,
                                              8, kAtlas, kPlayerRamp),
                      stop.value);
        const int after = layout::kSelectorValueX + 8 * static_cast<int>(stop.value.size());
        if (after < layout::kSelectorRightArrowX) {
            EXPECT_FALSE(named(content, glyphKey(after, layout::kSelectorY))) << stop.value;
        }
    }
}

// (8) The selector's arrows agree with the walk at every stop: each stands exactly where a press would
// move. With no rotations behind the screen the walk is the presets alone.
TEST(RotationScreen, TheArrowsAgreeWithTheWalkAtEveryStop) {
    for (const Stop& stop : everyStop()) {
        const auto content = spritesOf(draw(RotationScreenState{}, stop.settings, stop.rotations));
        const bool left  = kirpich::systems::rotationCanStep(stop.settings, &stop.rotations, -1);
        const bool right = kirpich::systems::rotationCanStep(stop.settings, &stop.rotations, +1);
        EXPECT_EQ(named(content, "rotation-left").has_value(), left) << stop.value;
        EXPECT_EQ(named(content, "rotation-right").has_value(), right) << stop.value;
        if (left) {
            expectSprites(content,
                          kirpich::render::Arrow("rotation-left", layout::kSelectorLeftArrowX,
                                                 layout::kSelectorY,
                                                 kirpich::render::ArrowDirection::LEFT,
                                                 layout::kArrowZ, kAtlas, kPlayerRamp),
                          stop.value);
        }
        if (right) {
            expectSprites(content,
                          kirpich::render::Arrow("rotation-right", layout::kSelectorRightArrowX,
                                                 layout::kSelectorY,
                                                 kirpich::render::ArrowDirection::RIGHT,
                                                 layout::kArrowZ, kAtlas, kPlayerRamp),
                          stop.value);
        }
    }
    EXPECT_FALSE(kirpich::systems::rotationCanStep(
        Settings{.paletteRotation = PaletteRotation::OFF}, nullptr, -1));
    EXPECT_FALSE(kirpich::systems::rotationCanStep(
        Settings{.paletteRotation = PaletteRotation::RAINBOW}, nullptr, +1));
    EXPECT_TRUE(kirpich::systems::rotationCanStep(
        Settings{.paletteRotation = PaletteRotation::RAINBOW}, nullptr, -1));
    EXPECT_TRUE(kirpich::systems::rotationCanStep(
        Settings{.paletteRotation = PaletteRotation::CUSTOM}, nullptr, -1));
    EXPECT_FALSE(kirpich::systems::rotationCanStep(
        Settings{.paletteRotation = PaletteRotation::CUSTOM}, nullptr, +1));
}

// (9) With no rotations behind the wiring the selector walks the presets alone: rainbow Right is an
// end stop, and no step reaches a slot.
TEST(RotationScreen, ANullWiringWalksThePresetsAlone) {
    Probe   probe;
    Session s(probe, /*wired=*/false);
    const CustomRotations before = probe.rotations;

    for (int i = 0; i < 3; ++i) s.step({Action::MenuRight});
    EXPECT_EQ(s.mode(), PaletteRotation::RAINBOW);
    s.step({Action::MenuRight});
    EXPECT_EQ(s.mode(), PaletteRotation::RAINBOW) << "an end stop";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    EXPECT_EQ(probe.rotations, before);
    EXPECT_EQ(probe.rotationSaves, 0);
}

// ── The picture ───────────────────────────────────────────────────────────────────────────────────

// (10) The grid shows the mode's ten palettes: each level's four colors, opaque, as squares of
// kGridSquare from its cell's swatch place, named `rotation-level-<i>-<shade>` - a preset's table, the
// working rotation for CUSTOM, and the player's own palette ten times for OFF, custom palette or not.
TEST(RotationScreen, TheGridShowsTheModesTenPalettes) {
    const auto expectGrid = [](const Settings& s, const CustomRotations& r,
                               const std::array<std::array<retropp::Rgba8, 4>, 10>& want,
                               std::string_view what) {
        const auto layers = draw(RotationScreenState{}, s, r);
        EXPECT_EQ(layers[1].regions.size(), 40u) << what;
        for (std::size_t level = 0; level < 10; ++level) {
            for (std::size_t shade = 0; shade < 4; ++shade) {
                const std::string key =
                    "rotation-level-" + std::to_string(level) + "-" + std::to_string(shade);
                const retropp::Region* got = region(layers, key);
                ASSERT_NE(got, nullptr) << what << " " << key;
                const auto x    = static_cast<float>(layout::gridSwatchX(level) +
                                                  static_cast<int>(shade) * layout::kGridSquare);
                const auto y    = static_cast<float>(layout::gridSwatchY(level));
                const auto side = static_cast<float>(layout::kGridSquare);
                ASSERT_EQ(got->shape.points.size(), 4u);
                EXPECT_EQ(got->shape.points[0], (retropp::Point{x, y})) << what << " " << key;
                EXPECT_EQ(got->shape.points[2], (retropp::Point{x + side, y + side}))
                    << what << " " << key;
                ASSERT_EQ(got->effects.size(), 1u);
                retropp::Rgba8 fill = want[level][shade];
                fill.a              = 255;
                EXPECT_EQ(got->effects[0].fill, fill) << what << " " << key;
            }
        }
    };

    const CustomRotations r = threeSlots();
    for (const PaletteRotation m :
         {PaletteRotation::HOTTER, PaletteRotation::COLDER, PaletteRotation::RAINBOW}) {
        std::array<std::array<retropp::Rgba8, 4>, 10> want{};
        for (std::size_t level = 0; level < 10; ++level) {
            want[level] = colorsOf((*kirpich::render::presetRotation(m))[level]);
        }
        expectGrid(Settings{.shadeRamp = kPlayerRamp, .paletteRotation = m}, r, want, "preset");
    }

    CustomRotations custom = threeSlots();
    custom.working         = distinct(120);
    std::array<std::array<retropp::Rgba8, 4>, 10> working{};
    for (std::size_t level = 0; level < 10; ++level) {
        working[level] = colorsOf(custom.working.levels[level]);
    }
    expectGrid(Settings{.shadeRamp = kPlayerRamp, .paletteRotation = PaletteRotation::CUSTOM},
               custom, working, "custom");

    for (const bool customPalette : {false, true}) {
        const Settings s{.shadeRamp = kPlayerRamp, .customPalette = customPalette};
        std::array<std::array<retropp::Rgba8, 4>, 10> own{};
        own.fill(kirpich::render::shadesOf(kAtlas, kirpich::paletteIndexOf(s)));
        expectGrid(s, custom, own, customPalette ? "off, custom palette" : "off, built-in ramp");
    }
}

// (11) The grid's digits stand over their swatches: `0`-`4` on the first row, `5`-`9` on the second,
// every one inside the screen.
TEST(RotationScreen, TheGridsDigitsStandOverTheirSwatches) {
    const auto content = spritesOf(draw(RotationScreenState{}, Settings{.shadeRamp = kPlayerRamp},
                                        threeSlots()));
    for (std::size_t level = 0; level < 10; ++level) {
        const int x = layout::gridDigitX(level);
        const int y = layout::gridDigitY(level);
        expectSprites(content,
                      kirpich::render::Glyphs(std::to_string(level), x, y, 8, kAtlas, kPlayerRamp),
                      "digit " + std::to_string(level));
        EXPECT_EQ(y, level < 5 ? layout::kGridFirstY : layout::kGridFirstY + layout::kGridRowPitch);
        EXPECT_LT(y + 8, layout::gridSwatchY(level) + 1) << "the digit stands over its swatch";
        EXPECT_GE(x, layout::gridSwatchX(level));
        EXPECT_LE(x + 8, layout::gridSwatchX(level) + 4 * layout::kGridSquare);
        EXPECT_GE(x, 0);
        EXPECT_LE(x + 8, 160);
    }
}

// (12) The editor item is present and dimmed, and A and Start do nothing on either row. The down page
// arrow stands at the shared place.
TEST(RotationScreen, TheEditorItemIsPresentAndDim) {
    const auto content = spritesOf(draw(RotationScreenState{}, Settings{.shadeRamp = kPlayerRamp},
                                        threeSlots()));
    expectSprites(content,
                  kirpich::render::DimGlyphs(layout::kEditorItem, layout::kLabelX, layout::kEditorY,
                                             8, kAtlas, kPlayerRamp),
                  "edit rotation");
    const auto down = named(content, "rotation-down");
    ASSERT_TRUE(down);
    EXPECT_EQ(down->x, 80);
    EXPECT_EQ(down->y, 128);
    EXPECT_FALSE(named(content, "rotation-up"));

    for (const RotationScreenRow row : {RotationScreenRow::EDITOR, RotationScreenRow::SELECTOR}) {
        for (const Action a : {Action::Confirm, Action::Start}) {
            Probe   probe;
            Session s(probe);
            s.ui().row                       = row;
            const GameContext     gameBefore = s.game;
            const Settings        before     = probe.settings;
            const CustomRotations rotations  = probe.rotations;
            s.step({a});
            EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_ROTATION);
            EXPECT_EQ(s.ui(), gameBefore.rotationScreen);
            EXPECT_EQ(s.sound(), SquareSfxId::NONE);
            EXPECT_EQ(probe.settings, before);
            EXPECT_EQ(probe.rotations, rotations);
        }
    }
}

// (13) The held preview is up exactly while Select is held - the held set, not the pressed set - and
// opening the screen or resetting its state takes it down.
TEST(RotationScreen, SelectHeldIsTheHeldPreview) {
    Probe   probe;
    Session s(probe);

    s.holdFor({Action::Select});
    EXPECT_TRUE(s.ui().previewing);
    s.holdFor({});
    EXPECT_FALSE(s.ui().previewing);
    s.holdFor({Action::MenuLeft});
    EXPECT_FALSE(s.ui().previewing) << "another button held is not the preview";
    s.step({Action::Select});
    EXPECT_TRUE(s.ui().previewing);

    kirpich::systems::openRotationScreen(s.game);
    EXPECT_FALSE(s.ui().previewing);
    s.ui().previewing = true;
    s.ui().reset();
    EXPECT_FALSE(s.ui().previewing);
}

// (14) While Select is held, Up and Right step the level forward and Down and Left step it back, 0-9
// with an end stop at each end, and nothing else does anything. On release the menu is where it was,
// and the level is kept.
TEST(RotationScreen, WhileHeldTheFourDirectionsStepTheLevelAndNothingElseRuns) {
    Probe   probe;
    Session s(probe);
    s.ui().row           = RotationScreenRow::SELECTOR;
    const Settings before = probe.settings;

    s.step({Action::Select, Action::MenuUp});
    EXPECT_EQ(s.ui().previewLevel, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::Select, Action::MenuRight});
    EXPECT_EQ(s.ui().previewLevel, 2);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::Select, Action::MenuDown});
    EXPECT_EQ(s.ui().previewLevel, 1);
    EXPECT_EQ(s.sound(), SquareSfxId::TINK);
    s.step({Action::Select, Action::MenuLeft});
    EXPECT_EQ(s.ui().previewLevel, 0);
    s.step({Action::Select, Action::MenuDown});
    EXPECT_EQ(s.ui().previewLevel, 0) << "an end stop at 0";
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);

    for (int i = 0; i < 12; ++i) s.step({Action::Select, Action::MenuUp});
    EXPECT_EQ(s.ui().previewLevel, 9);
    EXPECT_EQ(s.sound(), SquareSfxId::NONE) << "an end stop at 9";

    s.step({Action::Select, Action::Back});
    EXPECT_EQ(s.game.flow.gameState, GameState::PALETTE_ROTATION) << "B does not leave";
    s.step({Action::Select, Action::Confirm});
    EXPECT_EQ(s.sound(), SquareSfxId::NONE);
    EXPECT_EQ(s.ui().row, RotationScreenRow::SELECTOR) << "the cursor does not move";
    EXPECT_EQ(probe.settings, before) << "the selector does not step";
    EXPECT_EQ(probe.settingsSaves, 0);

    s.holdFor({});
    EXPECT_FALSE(s.ui().previewing);
    EXPECT_EQ(s.ui().row, RotationScreenRow::SELECTOR);
    EXPECT_EQ(s.ui().previewLevel, 9) << "the level is kept for the next hold";
    s.holdFor({Action::Select});
    EXPECT_EQ(s.ui().previewLevel, 9);
}

// (15) The held preview draws through the level's palette: the rotation set while a rotation is on,
// whatever the ramp and the custom palette say; the player's own palette while it is off.
TEST(RotationScreen, ThePreviewDrawsThroughTheLevelsPalette) {
    const RotationScreenState ui{.previewing = true, .previewLevel = 3};
    const CustomRotations     r = threeSlots();

    for (const PaletteRotation m : {PaletteRotation::HOTTER, PaletteRotation::COLDER,
                                    PaletteRotation::RAINBOW, PaletteRotation::CUSTOM}) {
        for (const bool customPalette : {false, true}) {
            const Settings s{.shadeRamp       = kPlayerRamp,
                             .customPalette   = customPalette,
                             .paletteRotation = m};
            const auto     layers = draw(ui, s, r);
            const auto     want   = handlesOf(kAtlas.rotation);
            for (const retropp::Sprite& sprite : spritesOf(layers)) {
                EXPECT_TRUE(want.contains(sprite.palette)) << sprite.key.value;
            }
            for (const retropp::TileCell& cell : cellsOf(layers)) {
                EXPECT_TRUE(want.contains(cell.palette));
            }
        }
    }

    for (const bool customPalette : {false, true}) {
        const Settings s{.shadeRamp = kPlayerRamp, .customPalette = customPalette};
        const auto     layers = draw(ui, s, r);
        const auto     want   = handlesOf(customPalette ? kAtlas.custom : kAtlas.palettes[kPlayerRamp]);
        for (const retropp::Sprite& sprite : spritesOf(layers)) {
            EXPECT_TRUE(want.contains(sprite.palette)) << sprite.key.value;
        }
        for (const retropp::TileCell& cell : cellsOf(layers)) {
            EXPECT_TRUE(want.contains(cell.palette));
        }
    }
}

// (16) The held preview's parts: the backdrop with its walls, the heading, the palette preview exactly,
// and the `level` item for the level, its arrows per the level - and nothing of the menu.
TEST(RotationScreen, ThePreviewsParts) {
    const Settings        s{.shadeRamp = kPlayerRamp, .paletteRotation = PaletteRotation::HOTTER};
    const CustomRotations r = threeSlots();

    for (const std::uint8_t level : {std::uint8_t{0}, std::uint8_t{5}, std::uint8_t{9}}) {
        const auto layers = draw(RotationScreenState{.previewing = true, .previewLevel = level}, s, r);
        ASSERT_EQ(layers.size(), 2u);
        EXPECT_EQ(layers[0].key.value, "rotation-backdrop");
        EXPECT_EQ(layers[1].key.value, "rotation-preview");
        EXPECT_EQ(layers[1].z, layout::kContentZ);

        const auto expectedCells = kirpich::render::PreviewBackdrop(kAtlas, kRotationRamp);
        const auto cells         = cellsOf(layers);
        ASSERT_EQ(cells.size(), expectedCells.size());
        for (std::size_t i = 0; i < cells.size(); ++i) {
            EXPECT_EQ(cells[i].tile, expectedCells[i].tile) << i;
            EXPECT_EQ(cells[i].palette, expectedCells[i].palette) << i;
            EXPECT_EQ(cells[i].atlas, expectedCells[i].atlas) << i;
        }

        const auto content  = spritesOf(layers);
        const auto heading  = kirpich::render::Glyphs(layout::kHeading, layout::kHeadingX,
                                                      layout::kHeadingY, 8, kAtlas, kRotationRamp);
        const auto preview  = kirpich::render::PalettePreview(kAtlas, kRotationRamp);
        const auto selector = kirpich::render::LevelSelector(
            "rotation-preview-level", level, layout::kPreviewLevelX, layout::kPreviewLevelY, kAtlas,
            kRotationRamp);
        expectSprites(content, heading, "heading");
        expectSprites(content, preview, "palette preview");
        expectSprites(content, selector, "level item");
        EXPECT_EQ(content.size(), heading.size() + preview.size() + selector.size());

        expectSprites(content,
                      kirpich::render::Glyphs(std::to_string(level),
                                              layout::kPreviewLevelX +
                                                  kirpich::render::kLevelSelectorNumberDx,
                                              layout::kPreviewLevelY, 8, kAtlas, kRotationRamp),
                      "the level's number");
        EXPECT_EQ(named(content, "rotation-preview-level-left").has_value(), level > 0);
        EXPECT_EQ(named(content, "rotation-preview-level-down").has_value(), level > 0);
        EXPECT_EQ(named(content, "rotation-preview-level-right").has_value(), level < 9);
        EXPECT_EQ(named(content, "rotation-preview-level-up").has_value(), level < 9);

        EXPECT_FALSE(named(content, "rotation-down"));
        EXPECT_FALSE(named(content, "rotation-left"));
        EXPECT_FALSE(named(content, "rotation-right"));
        EXPECT_FALSE(named(content, glyphKey(layout::kCursorX, layout::kSelectorY)));
        EXPECT_TRUE(layers[1].regions.empty());
    }
}

// (17) Every frame names each object once and stays on the screen - and while previewing, between the
// walls - swept over the preview up and down, every stop, both rows, both blink states and three
// levels.
TEST(RotationScreen, EveryFrameNamesEachObjectOnceAndStaysOnScreen) {
    for (const Stop& stop : everyStop()) {
        for (const bool previewing : {false, true}) {
            for (const RotationScreenRow row :
                 {RotationScreenRow::EDITOR, RotationScreenRow::SELECTOR}) {
                for (const bool blinkOn : {false, true}) {
                    for (const std::uint8_t level : {std::uint8_t{0}, std::uint8_t{4}, std::uint8_t{9}}) {
                        const RotationScreenState ui{
                            .row = row, .previewing = previewing, .previewLevel = level};
                        const auto layers = draw(ui, stop.settings, stop.rotations, blinkOn);
                        std::set<std::string> keys;
                        for (const retropp::Sprite& s : spritesOf(layers)) {
                            EXPECT_TRUE(keys.insert(s.key.value).second)
                                << "two objects named " << s.key.value;
                            EXPECT_GE(s.x, 0) << s.key.value;
                            EXPECT_GE(s.y, 0) << s.key.value;
                            EXPECT_LE(s.x + 8, 160) << s.key.value;
                            EXPECT_LE(s.y + 8, 144) << s.key.value;
                            if (previewing) {
                                EXPECT_GE(s.x, kirpich::render::kPreviewWallLeftX +
                                                   kirpich::render::kPreviewBrickSide)
                                    << s.key.value;
                                EXPECT_LE(s.x + 8, kirpich::render::kPreviewWallRightX)
                                    << s.key.value;
                            }
                        }
                        for (const retropp::Region& r : layers[1].regions) {
                            EXPECT_TRUE(keys.insert(r.key.value).second)
                                << "two objects named " << r.key.value;
                        }
                    }
                }
            }
        }
    }
}

// ── The shared components ─────────────────────────────────────────────────────────────────────────

// (18) ColorSwatch is four abutting opaque squares from (x, y), named `<stem>-<i>`; LevelSelector is
// the word, the number and the arrows at their offsets, its arrows present only where a step exists,
// and kLevelSelectorWidth reaches the right arrow's right edge.
TEST(RotationScreen, TheColorSwatchAndTheLevelSelector) {
    const std::array<retropp::Rgba8, 4> colors{
        retropp::Rgba8{.r = 1, .g = 2, .b = 3, .a = 0}, retropp::Rgba8{.r = 4, .g = 5, .b = 6, .a = 9},
        retropp::Rgba8{.r = 7, .g = 8, .b = 9, .a = 128},
        retropp::Rgba8{.r = 10, .g = 11, .b = 12, .a = 255}};
    const kirpich::render::Regions swatch = kirpich::render::ColorSwatch("probe", colors, 30, 40, 5);
    ASSERT_EQ(swatch.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        const retropp::Region& r = swatch[i];
        EXPECT_EQ(r.key.value, "probe-" + std::to_string(i));
        const auto left = static_cast<float>(30 + 5 * static_cast<int>(i));
        EXPECT_EQ(r.shape.points,
                  (std::vector<retropp::Point>{{left, 40.0f}, {left + 5, 40.0f}, {left + 5, 45.0f},
                                               {left, 45.0f}}));
        ASSERT_EQ(r.effects.size(), 1u);
        EXPECT_EQ(r.effects[0].kind, retropp::ScreenSpaceEffectKind::ColorFill);
        retropp::Rgba8 want = colors[i];
        want.a              = 255;
        EXPECT_EQ(r.effects[0].fill, want) << "opaque whatever the color's own alpha";
    }

    constexpr int x = 20;
    constexpr int y = 60;
    for (const std::uint8_t level : {std::uint8_t{0}, std::uint8_t{4}, std::uint8_t{9}}) {
        const kirpich::render::Sprites item =
            kirpich::render::LevelSelector("probe", level, x, y, kAtlas, kPlayerRamp);
        const int numberX = x + kirpich::render::kLevelSelectorNumberDx;
        expectSprites(item, kirpich::render::Glyphs("level", x, y, 8, kAtlas, kPlayerRamp), "word");
        expectSprites(item,
                      kirpich::render::Glyphs(std::to_string(level), numberX, y, 8, kAtlas,
                                              kPlayerRamp),
                      "number");
        using kirpich::render::ArrowDirection;
        const auto arrow = [&](std::string key, int ax, int ay, ArrowDirection d) {
            return kirpich::render::Arrow(std::move(key), ax, ay, d,
                                          kirpich::render::kLevelSelectorArrowZ, kAtlas, kPlayerRamp);
        };
        if (level > 0) {
            expectSprites(item, arrow("probe-left", numberX - 8, y, ArrowDirection::LEFT), "left");
            expectSprites(item, arrow("probe-down", numberX, y + 8, ArrowDirection::DOWN), "down");
        }
        if (level < 9) {
            expectSprites(item, arrow("probe-right", numberX + 8, y, ArrowDirection::RIGHT), "right");
            expectSprites(item, arrow("probe-up", numberX, y - 8, ArrowDirection::UP), "up");
        }
        EXPECT_EQ(named(item, "probe-left").has_value(), level > 0);
        EXPECT_EQ(named(item, "probe-down").has_value(), level > 0);
        EXPECT_EQ(named(item, "probe-right").has_value(), level < 9);
        EXPECT_EQ(named(item, "probe-up").has_value(), level < 9);
        const std::size_t arrows = (level > 0 ? 2u : 0u) + (level < 9 ? 2u : 0u);
        EXPECT_EQ(item.size(), 5u + 1u + arrows) << "the word, the digit and the arrows";
    }
    EXPECT_EQ(kirpich::render::kLevelSelectorWidth, (x + kirpich::render::kLevelSelectorNumberDx + 8 +
                                                     8) -
                                                        x);
}
