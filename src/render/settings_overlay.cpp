#include "render/settings_overlay.h"

#include "render/components/page_arrows.h"
#include "state/screen_ui_state.h"  // settingsPageOf, kSettingsPageCount
#include "systems/list_screen.h"    // the list's window height

namespace kirpich::render {

std::vector<retropp::Sprite> settingsPageArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                                const TileAtlas& atlas) {
    const std::uint8_t page = kirpich::settingsPageOf(ui.settingsRow);
    return PageArrows("settings-page", page > 0, page + 1 < kirpich::kSettingsPageCount, atlas,
                      ramp);
}

std::vector<retropp::Sprite> carouselArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                            const TileAtlas& atlas, std::size_t optionCount) {
    // Moving between options is moving between screens, so it says what a page arrow says everywhere
    // else. An arrow is drawn only where there is an option to reach.
    const auto shown = static_cast<std::size_t>(ui.carouselOption);
    return PageArrows("carousel", shown > 0, optionCount != 0 && shown + 1 < optionCount, atlas,
                      ramp);
}

std::vector<retropp::Sprite> listArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                        const TileAtlas& atlas) {
    // The window's own edges: rows above it, and rows below it. Scrolling a list is moving between
    // screenfuls of it.
    const auto top = static_cast<std::size_t>(ui.listTop);
    return PageArrows("list", top > 0, top + kirpich::systems::kListRows < ui.listCount, atlas,
                      ramp);
}

std::vector<retropp::Sprite> statsPageArrows(const kirpich::ScreenUiState& ui, std::uint8_t ramp,
                                             const TileAtlas& atlas) {
    // How many pages the branch holds was recorded by the screen as it painted, because the branch's
    // own count is a seam the render bridge cannot reach.
    const auto page = static_cast<std::size_t>(ui.statsPage);
    return PageArrows("stats-page", page > 0, page + 1 < ui.statsPageCount, atlas, ramp);
}

}  // namespace kirpich::render
