#include "render/settings_overlay.h"

#include <string>
#include <string_view>

#include "render/scroller_arrows.h"   // kSelectorTile
#include "systems/list_screen.h"      // the list's window height
#include "systems/settings_screen.h"  // the cell coordinates the arrows line up with

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// Above every sprite the object buffer can produce, so a page arrow is never hidden behind one.
constexpr std::int32_t kPageArrowZ = 100;

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

}  // namespace kirpich::render
