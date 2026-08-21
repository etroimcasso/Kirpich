// The settings screen's drawn parts — behavioral tests over src/render/settings_overlay.h.
//
// Device-free: the overlay is a pure function from the screen's state to a list of regions, with no
// renderer and no device. These are the port's own screens, so every asserted value comes from the
// surface's stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "render/palettes.h"
#include "render/settings_overlay.h"
#include "state/screen_ui_state.h"

namespace {

using kirpich::ScreenUiState;
using kirpich::SettingsRow;
using kirpich::render::kShadeRampCount;

constexpr int kViewportWidth = 160;

// A shape's vertex count says what it is: three for an arrow, four for a preview square.
constexpr std::size_t kArrowPoints  = 3;
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

// (2) A scroll arrow is drawn only where there is somewhere to scroll to, so the ends of the range
// are visible rather than something a player finds by pressing.
TEST(SettingsOverlay, ScrollArrowsVanishAtTheEndsOfTheRange) {
    const auto arrowsFor = [](std::uint8_t ramp) {
        const auto regions =
            kirpich::render::settingsOverlay(on(SettingsRow::SHADE_RAMP), ramp, kViewportWidth);
        // One of the arrows is the page arrow, which is always there on the first page.
        return countWith(regions, kArrowPoints);
    };

    EXPECT_EQ(arrowsFor(0), 2u) << "first ramp: a right arrow and the page arrow, no left";
    EXPECT_EQ(arrowsFor(kShadeRampCount - 1), 2u) << "last ramp: a left arrow and the page arrow";
    for (std::uint8_t ramp = 1; ramp + 1 < kShadeRampCount; ++ramp) {
        EXPECT_EQ(arrowsFor(ramp), 3u) << "middle ramp " << +ramp << ": both arrows and the page one";
    }
}

// (3) The page arrow points at the page that is there, and only that one. The second page carries no
// palette scroller at all, since the palette row is not on it.
TEST(SettingsOverlay, PageArrowPointsAtThePageThatExists) {
    // First page: one page arrow, and it points down.
    {
        const auto regions =
            kirpich::render::settingsOverlay(on(SettingsRow::FULLSCREEN), 0, kViewportWidth);
        EXPECT_EQ(countWith(regions, kSquarePoints), 4u) << "the preview belongs to the first page";

        bool sawDown = false;
        for (const auto& region : regions) {
            if (region.shape.points.size() != kArrowPoints) continue;
            // A downward arrow's apex is below its base; an upward one's is above.
            const float apexY = region.shape.points[0].y;
            const float baseY = region.shape.points[1].y;
            if (apexY > baseY) sawDown = true;
        }
        EXPECT_TRUE(sawDown) << "the first page must say there is one below it";
    }

    // Second page: an upward arrow, and nothing belonging to the palette row.
    {
        const auto regions =
            kirpich::render::settingsOverlay(on(SettingsRow::RESET_SCORES), 3, kViewportWidth);
        EXPECT_EQ(countWith(regions, kSquarePoints), 0u) << "no preview on the second page";
        ASSERT_EQ(countWith(regions, kArrowPoints), 1u) << "only the page arrow";

        const auto& arrow = regions.front();
        EXPECT_LT(arrow.shape.points[0].y, arrow.shape.points[1].y)
            << "the second page's arrow must point up";
    }
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
