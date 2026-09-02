#include "render/settings_overlay.h"

#include <array>
#include <string>
#include <string_view>

#include "render/palettes.h"
#include "systems/list_screen.h"      // the list's window height
#include "systems/settings_screen.h"  // the cell coordinates the drawn parts line up with

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// The game's own selector arrow, the same tile the settings screen puts either side of the palette
// number. It points right; a quarter turn stands it up.
constexpr std::uint8_t kSelectorTile = 0x58;

// Above every sprite the object buffer can produce, so a page arrow is never hidden behind one.
constexpr std::int32_t kPageArrowZ = 100;

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

// One screen's two page arrows: the game's own selector tile stood on end, at the shared column, one
// above the heading and one below the body.
//
// Every screen that draws these differs in nothing but the two conditions and the name it gives each
// sprite, so they share one placement rather than each carrying a copy of it. The names are the
// caller's because a name is what stops the renderer easing one screen's arrow into the next one's.
std::vector<retropp::Sprite> pageArrows(std::string_view keyStem, bool up, bool down,
                                        std::uint8_t ramp, const TileAtlas& atlas) {
    // The screens these are drawn over select the copyright-and-title art while they are up, which is
    // the set the selector tile belongs to.
    const ResolvedTile art = resolveSpriteTile(kSelectorTile, kirpich::TileSheet::COPYRIGHT_TITLE,
                                               /*palette1=*/false, atlas, ramp);

    std::vector<retropp::Sprite> sprites;
    const auto arrow = [&](std::string key, std::size_t row, retropp::Rotation turn) {
        sprites.push_back(retropp::Sprite{
            .key      = retropp::ObjectKey{std::move(key)},
            .x        = static_cast<int>(kirpich::systems::kPageArrowCol) * kCell,
            .y        = static_cast<int>(row) * kCell,
            // Above the object buffer's own sprites, which number at most one per buffer entry.
            .z        = kPageArrowZ,
            .atlas    = art.atlas,
            .tile     = art.cell,
            .palette  = art.palette,
            .rotation = turn,
        });
    };

    // The selector points right, so a quarter turn stands it up. An arrow is drawn only where there
    // is somewhere to go that way.
    if (up) {
        arrow(std::string{keyStem} + "-up", kirpich::systems::kPageUpArrowRow,
              retropp::Rotation::Rot270);
    }
    if (down) {
        arrow(std::string{keyStem} + "-down", kirpich::systems::kPageDownArrowRow,
              retropp::Rotation::Rot90);
    }
    return sprites;
}

}  // namespace

std::vector<retropp::Sprite> settingsPageArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                                const TileAtlas& atlas) {
    const std::uint8_t page = kirpich::settingsPageOf(ui.settingsRow);
    return pageArrows("settings-page", page > 0, page + 1 < kirpich::kSettingsPageCount, ramp,
                      atlas);
}

std::vector<retropp::Sprite> carouselArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                            const TileAtlas& atlas, std::size_t optionCount) {
    // Moving between options is moving between screens, so it says what a page arrow says everywhere
    // else. An arrow is drawn only where there is an option to reach.
    const auto shown = static_cast<std::size_t>(ui.carouselOption);
    return pageArrows("carousel", shown > 0, optionCount != 0 && shown + 1 < optionCount, ramp,
                      atlas);
}

std::vector<retropp::Sprite> listArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                        const TileAtlas& atlas) {
    // The window's own edges: rows above it, and rows below it. Scrolling a list is moving between
    // screenfuls of it.
    const auto top = static_cast<std::size_t>(ui.listTop);
    return pageArrows("list", top > 0, top + kirpich::systems::kListRows < ui.listCount, ramp,
                      atlas);
}

std::vector<retropp::Sprite> statsPageArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                             const TileAtlas& atlas) {
    // How many pages the branch holds was recorded by the screen as it painted, because the branch's
    // own count is a seam the render bridge cannot reach.
    const auto page = static_cast<std::size_t>(ui.statsPage);
    return pageArrows("stats-page", page > 0, page + 1 < ui.statsPageCount, ramp, atlas);
}

std::vector<retropp::Region> settingsOverlay(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                             int viewportWidth) {
    const std::uint8_t                  chosen  = clampShadeRamp(ramp);
    const std::array<retropp::Rgba8, 4> colours = rampColours(chosen);
    const std::uint8_t                  page    = kirpich::settingsPageOf(ui.settingsRow);

    std::vector<retropp::Region> regions;
    regions.reserve(colours.size());

    // The two arrows either side of the palette number are objects the settings screen places, and the
    // page arrow is a sprite (settingsPageArrows) - both are the game's own selector tile. What is
    // left here is the one thing the art has no tile for: colour.
    //
    // The preview lives on the first page only, with the row it previews.
    if (page != 0) {
        return regions;
    }

    // The scroller's own two arrows are objects rather than shapes: the game already has a selector
    // arrow of its own, and the left one is that tile flipped. The settings screen places them.

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
