#include "render/settings_overlay.h"

#include <array>
#include <string>

#include "render/palettes.h"
#include "systems/settings_screen.h"  // the cell coordinates the drawn parts line up with

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// The colour the arrows are drawn in: the darkest shade of the ramp on show, so they read as ink on
// the screen's own paper whichever ramp that is.
retropp::Rgba8 inkFor(std::uint8_t ramp) {
    retropp::Rgba8 ink = rampColours(ramp)[0];
    ink.a              = 255;
    return ink;
}

retropp::Region filled(std::string key, retropp::ShapePoints shape, retropp::Rgba8 fill) {
    return retropp::Region{
        // Regions are not interpolated, so a key need only be present - one each keeps them distinct
        // in anything that reports drawables by name.
        .key     = retropp::ObjectKey{std::move(key)},
        .shape   = std::move(shape),
        .effects = {retropp::ScreenSpaceEffect{.kind = retropp::ScreenSpaceEffectKind::ColorFill,
                                               .fill = fill}},
    };
}

// Which way an arrow points.
enum class Point { Left, Right, Up, Down };

// A solid triangle filling one cell. Its apex sits on the middle of the edge it points at and its
// base spans the opposite edge, both inset so the arrow does not touch its neighbours.
retropp::ShapePoints arrow(std::size_t col, std::size_t row, Point points) {
    const auto left    = static_cast<float>(col * kCell + kArrowInset);
    const auto right   = static_cast<float>((col + 1) * kCell - kArrowInset);
    const auto top     = static_cast<float>(row * kCell + kArrowInset);
    const auto bottom  = static_cast<float>((row + 1) * kCell - kArrowInset);
    const auto midX    = (left + right) / 2.0f;
    const auto midY    = (top + bottom) / 2.0f;

    switch (points) {
        case Point::Left:  return {.points = {{left, midY}, {right, top}, {right, bottom}}};
        case Point::Right: return {.points = {{right, midY}, {left, top}, {left, bottom}}};
        case Point::Up:    return {.points = {{midX, top}, {left, bottom}, {right, bottom}}};
        case Point::Down:  break;
    }
    return {.points = {{midX, bottom}, {left, top}, {right, top}}};
}

}  // namespace

std::vector<retropp::Region> settingsOverlay(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                             int viewportWidth) {
    const std::uint8_t                  chosen  = clampShadeRamp(ramp);
    const std::array<retropp::Rgba8, 4> colours = rampColours(chosen);
    const retropp::Rgba8                ink     = inkFor(chosen);
    const std::uint8_t                  page    = kirpich::settingsPageOf(ui.settingsRow);

    std::vector<retropp::Region> regions;
    regions.reserve(colours.size() + 3);

    // The arrow that says another page is there, at the edge it lies past.
    if (page > 0) {
        regions.push_back(filled(
            "settings-page-up",
            arrow(systems::kPageArrowCol, systems::kPageUpArrowRow, Point::Up), ink));
    }
    if (page + 1 < kirpich::kSettingsPageCount) {
        regions.push_back(filled(
            "settings-page-down",
            arrow(systems::kPageArrowCol, systems::kPageDownArrowRow, Point::Down), ink));
    }

    // The palette scroller and its preview live on the first page only.
    if (page != 0) {
        return regions;
    }

    const std::size_t rampRow = systems::settingsRowLine(kirpich::SettingsRow::SHADE_RAMP);
    if (chosen > 0) {
        regions.push_back(filled("palette-arrow-left",
                                 arrow(systems::kPaletteLeftArrowCol, rampRow, Point::Left), ink));
    }
    if (chosen + 1 < kShadeRampCount) {
        regions.push_back(filled("palette-arrow-right",
                                 arrow(systems::kPaletteRightArrowCol, rampRow, Point::Right), ink));
    }

    // The preview: four squares, darkest to lightest, abutting so the strip reads as one band of
    // colour rather than as four separate blocks.
    const int left = (viewportWidth - kSwatchWidth) / 2;
    const auto top = static_cast<float>(systems::kPaletteSwatchRow * kCell);
    for (std::size_t i = 0; i < colours.size(); ++i) {
        const auto x = static_cast<float>(left + static_cast<int>(i) * kSwatchSquare);
        const auto w = static_cast<float>(kSwatchSquare);

        retropp::Rgba8 fill = colours[i];
        fill.a              = 255;

        regions.push_back(filled(
            "palette-swatch-" + std::to_string(i),
            retropp::ShapePoints{
                .points = {{x, top}, {x + w, top}, {x + w, top + w}, {x, top + w}}},
            fill));
    }
    return regions;
}

}  // namespace kirpich::render
