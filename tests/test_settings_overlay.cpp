// The shade ramps, and the page arrows of the screens drawn into the map — behavioral tests over
// src/render/palettes.h and src/render/settings_overlay.h.
//
// Device-free: the arrows are a pure function from the screen's state to a list of sprites, with no
// renderer and no device. These are the port's own screens, so every asserted value comes from the
// surface's stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "render/palettes.h"
#include "render/tile_atlas.h"
#include "render/settings_overlay.h"
#include "state/screen_ui_state.h"
#include "systems/settings_screen.h"  // the heading row the arrows are placed against

namespace {

using kirpich::ScreenUiState;
using kirpich::SettingsRow;
using kirpich::render::kShadeRampCount;
using kirpich::render::TileAtlas;

ScreenUiState on(SettingsRow row) {
    ScreenUiState ui;
    ui.settingsRow = row;
    return ui;
}

// (0) Every ramp runs dark to light, which is what the whole scheme rests on: the art stores a sample
// per pixel and the ramp says what that sample is worth, so a ramp whose shades are out of order
// draws the game inverted or muddy. Swept over every ramp the build offers, so authoring a new one
// out of order fails here rather than on screen.
TEST(ShadeRamps, EveryRampRunsDarkToLight) {
    const auto luminance = [](retropp::Rgba8 c) {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
    };

    for (std::uint8_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        const auto colours = kirpich::render::rampColours(ramp);
        for (std::size_t i = 0; i + 1 < colours.size(); ++i) {
            EXPECT_LT(luminance(colours[i]), luminance(colours[i + 1]))
                << "ramp " << (ramp + 1) << ", shade " << i << " is not darker than shade "
                << (i + 1);
        }
    }
}

// (0a) Every ramp is held to what it says about its own darkest shade. A ramp that does not declare
// `mayBottomOutAtBlack` has to keep a real colour down there — the darkest shade is what the field's
// walls and every locked block are drawn in, so a ramp that quietly went black there would look like
// the many ramps that already do, whatever its other three shades were chosen to be.
//
// The declaration is what makes this checkable per ramp rather than by position, so a ramp can be
// added anywhere in the list and is still asked the right question.
//
// Two measurements, because either alone passes for the wrong reason: chroma alone accepts a bright
// colour, luminance alone accepts a dark grey.
TEST(ShadeRamps, EveryRampHoldsToWhatItSaysAboutItsDarkestShade) {
    const auto luminance = [](retropp::Rgba8 c) {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
    };
    const auto chroma = [](retropp::Rgba8 c) {
        return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
    };

    std::size_t colourKeeping = 0;
    for (std::size_t i = 0; i < kShadeRampCount; ++i) {
        const kirpich::render::ShadeRamp& ramp = kirpich::render::kShadeRamps[i];
        if (ramp.mayBottomOutAtBlack) {
            continue;  // it says so; nothing to hold it to
        }
        ++colourKeeping;

        const retropp::Rgba8 darkest = ramp.darkest;
        EXPECT_GE(chroma(darkest), 30)
            << "ramp " << (i + 1) << " does not declare mayBottomOutAtBlack, but its darkest shade is "
            << "very close to grey";
        EXPECT_GT(luminance(darkest), 35.0)
            << "ramp " << (i + 1) << " does not declare mayBottomOutAtBlack, but its darkest shade is "
            << "too near black to read as a colour";

        // Still the darkest of its four, and still leaving the others room to climb away from it.
        EXPECT_LT(luminance(darkest), 110.0) << "ramp " << (i + 1) << "'s darkest shade is not dark";
        EXPECT_GT(luminance(ramp.lightest) - luminance(darkest), 100.0)
            << "ramp " << (i + 1) << " has too little range between its ends";
    }

    EXPECT_GT(colourKeeping, 0u) << "the check has to be asking something of somebody";
}

// (0c) No two ramps are the same ramp. The list has grown four times and three names already appear
// twice with different colours (moss, wine, forest), so sameness is asked of the values, not the
// comments — a ramp authored as an accidental repeat of one that exists is a wasted slot the player
// pages past, and nothing but this would ever notice it.
TEST(ShadeRamps, NoTwoRampsAreTheSame) {
    for (std::size_t a = 0; a < kShadeRampCount; ++a) {
        for (std::size_t b = a + 1; b < kShadeRampCount; ++b) {
            EXPECT_FALSE(kirpich::render::kShadeRamps[a] == kirpich::render::kShadeRamps[b])
                << "ramps " << (a + 1) << " and " << (b + 1) << " are identical";
        }
    }
}

// (0b) The default is the first ramp, and it is the greyscale the hardware's own shades map to — so a
// player who never opens the settings screen sees what the game always looked like.
TEST(ShadeRamps, DefaultIsTheHardwareGreyscale) {
    EXPECT_EQ(kirpich::render::kDefaultShadeRamp, 0);
    const auto grey = kirpich::render::rampColours(kirpich::render::kDefaultShadeRamp);
    for (const retropp::Rgba8 shade : grey) {
        EXPECT_EQ(shade.r, shade.g) << "the default ramp must be neutral";
        EXPECT_EQ(shade.g, shade.b) << "the default ramp must be neutral";
    }
    EXPECT_EQ(grey.front().r, 0x00);
    EXPECT_EQ(grey.back().r, 0xFF);
}

// (3) The page arrow is the game's own selector tile stood on end, and it points at the page that is
// actually there. It is a sprite because an object carries only the hardware's two flips, and no flip
// stands a sideways triangle upright.
TEST(SettingsOverlay, PageArrowIsTheSelectorTurnedAQuarter) {
    constexpr std::uint8_t kSelectorTile = 0x58;

    // Recognisable handles, so a wrong sheet or palette is loud.
    TileAtlas atlas;
    atlas.copyrightTitle = static_cast<retropp::AtlasId>(22);
    for (std::size_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        atlas.palettes[ramp].sprite0 = static_cast<retropp::PaletteId>(70 + ramp);
    }

    const auto expected = kirpich::render::resolveSpriteTile(
        kSelectorTile, kirpich::TileSheet::COPYRIGHT_TITLE, false, atlas, 0);

    // The first page: one arrow, turned to point down at the page below it.
    {
        const auto arrows =
            kirpich::render::settingsPageArrows(on(SettingsRow::DISPLAY), 0, atlas);
        ASSERT_EQ(arrows.size(), 1u);
        EXPECT_EQ(arrows[0].tile, expected.cell) << "the game's own selector, not a new tile";
        EXPECT_EQ(arrows[0].atlas, expected.atlas);
        EXPECT_EQ(arrows[0].palette, expected.palette);
        EXPECT_EQ(arrows[0].rotation, retropp::Rotation::Rot90);
        EXPECT_FALSE(arrows[0].flipX) << "a flip cannot stand it up; the rotation does";
    }

    // The last page: one arrow, turned the other way.
    {
        const auto arrows =
            kirpich::render::settingsPageArrows(on(SettingsRow::RESET_SCORES), 0, atlas);
        ASSERT_EQ(arrows.size(), 1u);
        EXPECT_EQ(arrows[0].tile, expected.cell);
        EXPECT_EQ(arrows[0].rotation, retropp::Rotation::Rot270)
            << "the two page arrows are the same tile turned opposite ways";
    }

    // Whichever ramp is on, the arrow is coloured by that ramp's object palette.
    for (std::uint8_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        const auto arrows =
            kirpich::render::settingsPageArrows(on(SettingsRow::DISPLAY), ramp, atlas);
        ASSERT_EQ(arrows.size(), 1u);
        EXPECT_EQ(arrows[0].palette, static_cast<retropp::PaletteId>(70 + ramp))
            << "ramp " << +ramp;
    }
}

// (3a-ii) Where the page arrows stand, which is what they mean. An arrow drawn over a screen's text
// says the TEXT moves; an arrow above the heading says the SCREEN does. The up arrow therefore sits
// above the heading and the down arrow below the body, and both are taken from one place so the
// settings screen and the screens it opens cannot disagree about it.
TEST(SettingsOverlay, PageUpArrowStandsAboveTheHeading) {
    constexpr int kCell = 8;

    TileAtlas atlas;
    atlas.copyrightTitle = static_cast<retropp::AtlasId>(22);

    const int headingY = static_cast<int>(kirpich::systems::kScreenTitleRow) * kCell;

    // The last page is the one with somewhere above it to go.
    const auto up = kirpich::render::settingsPageArrows(on(SettingsRow::RESET_SCORES), 0, atlas);
    ASSERT_EQ(up.size(), 1u);
    EXPECT_EQ(up[0].rotation, retropp::Rotation::Rot270);
    EXPECT_LT(up[0].y, headingY)
        << "the page-up arrow stands above the heading, or it reads as the page's text scrolling";

    // The first page's arrow points down, and stands below the heading rather than over it.
    const auto down = kirpich::render::settingsPageArrows(on(SettingsRow::DISPLAY), 0, atlas);
    ASSERT_EQ(down.size(), 1u);
    EXPECT_EQ(down[0].rotation, retropp::Rotation::Rot90);
    EXPECT_GT(down[0].y, headingY);

    // The up arrow's row is derived from the heading's rather than written as a number of its own,
    // which is what stops a screen placing one under its heading.
    EXPECT_EQ(kirpich::systems::kPageUpArrowRow + 1, kirpich::systems::kScreenTitleRow);
}

}  // namespace
