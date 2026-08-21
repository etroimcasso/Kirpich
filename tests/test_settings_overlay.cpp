// The settings screen's drawn parts — behavioral tests over src/render/settings_overlay.h.
//
// Device-free: the overlay is a pure function from the screen's state to a list of regions, with no
// renderer and no device. These are the port's own screens, so every asserted value comes from the
// surface's stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "render/palettes.h"
#include "render/tile_atlas.h"
#include "render/settings_overlay.h"
#include "state/screen_ui_state.h"

namespace {

using kirpich::ScreenUiState;
using kirpich::SettingsRow;
using kirpich::render::kShadeRampCount;
using kirpich::render::TileAtlas;

constexpr int kViewportWidth = 160;

// A shape's vertex count says what it is. Only the preview squares are shapes now — every arrow on
// the screen is the game's own selector tile, drawn as an object or a sprite.
constexpr std::size_t kSquarePoints = 4;

std::size_t countWith(const std::vector<retropp::Region>& regions, std::size_t points) {
    std::size_t n = 0;
    for (const auto& region : regions) {
        if (region.shape.points.size() == points) ++n;
    }
    return n;
}

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

// (1) The preview strip is the chosen ramp's four colours, in ramp order, opaque — and the squares
// abut, which is what makes the strip read as one palette rather than as four blocks. Swept over
// every ramp, so a ramp whose colours were routed wrong fails here.
TEST(SettingsOverlay, PreviewShowsTheRampsFourColoursAdjacent) {
    for (std::uint8_t ramp = 0; ramp < kShadeRampCount; ++ramp) {
        const auto regions = kirpich::render::settingsOverlay(on(SettingsRow::SHADE_RAMP), ramp,
                                                              kViewportWidth);
        const auto colours = kirpich::render::rampColours(ramp);

        ASSERT_EQ(countWith(regions, kSquarePoints), colours.size()) << "ramp " << +ramp;

        std::size_t seen = 0;
        float       previousRight = 0.0f;
        for (const auto& region : regions) {
            if (region.shape.points.size() != kSquarePoints) continue;

            ASSERT_EQ(region.effects.size(), 1u);
            EXPECT_EQ(region.effects[0].kind, retropp::ScreenSpaceEffectKind::ColorFill);
            EXPECT_EQ(region.effects[0].fill.r, colours[seen].r) << "ramp " << +ramp;
            EXPECT_EQ(region.effects[0].fill.g, colours[seen].g) << "ramp " << +ramp;
            EXPECT_EQ(region.effects[0].fill.b, colours[seen].b) << "ramp " << +ramp;
            EXPECT_EQ(region.effects[0].fill.a, 255) << "a preview square is opaque";

            const float left  = region.shape.points[0].x;
            const float right = region.shape.points[1].x;
            if (seen > 0) {
                EXPECT_FLOAT_EQ(left, previousRight) << "square " << seen << " must touch the last";
            }
            previousRight = right;
            ++seen;
        }
    }
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
            kirpich::render::settingsPageArrows(on(SettingsRow::FULLSCREEN), 0, atlas);
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
            kirpich::render::settingsPageArrows(on(SettingsRow::FULLSCREEN), ramp, atlas);
        ASSERT_EQ(arrows.size(), 1u);
        EXPECT_EQ(arrows[0].palette, static_cast<retropp::PaletteId>(70 + ramp))
            << "ramp " << +ramp;
    }
}

// (3b) The second page carries no palette preview, since the row it previews is not on it.
TEST(SettingsOverlay, SecondPageHasNoPreview) {
    const auto regions =
        kirpich::render::settingsOverlay(on(SettingsRow::RESET_SCORES), 3, kViewportWidth);
    EXPECT_TRUE(regions.empty()) << "nothing on the second page is a shape";
}

// (4) The strip is centred across the viewport, whatever the viewport is.
TEST(SettingsOverlay, PreviewIsCentredInTheViewport) {
    for (const int width : {160, 240, 320}) {
        const auto regions =
            kirpich::render::settingsOverlay(on(SettingsRow::SHADE_RAMP), 0, width);

        float left = 0.0f, right = 0.0f;
        bool  first = true;
        for (const auto& region : regions) {
            if (region.shape.points.size() != kSquarePoints) continue;
            if (first) {
                left  = region.shape.points[0].x;
                first = false;
            }
            right = region.shape.points[1].x;
        }
        ASSERT_FALSE(first) << "no preview squares at width " << width;
        EXPECT_FLOAT_EQ(left, static_cast<float>((width - kirpich::render::kSwatchWidth) / 2));
        EXPECT_FLOAT_EQ(right - left, static_cast<float>(kirpich::render::kSwatchWidth));
    }
}

}  // namespace
