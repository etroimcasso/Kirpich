#include "render/heart_indicator.h"

#include <kirpich/char_tile.h>

#include "state/display_state.h"   // TileSheet
#include "systems/menu_screens.h"  // where the heading is

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// Above the object buffer, so the heart is never hidden behind a cursor a screen left on the display.
constexpr std::int32_t kHeartZ = 200;

}  // namespace

bool heartIndicatorShown(kirpich::GameState state, std::uint8_t heartMode) noexcept {
    if (heartMode == 0) {
        return false;
    }
    switch (state) {
        case kirpich::GameState::INIT_TYPE_A_DIFFICULTY:
        case kirpich::GameState::TYPE_A_LEVEL_SELECTION:
        case kirpich::GameState::INIT_TYPE_B_DIFFICULTY:
        case kirpich::GameState::TYPE_B_LEVEL_SELECTION:
        case kirpich::GameState::TYPE_B_HEIGHT_SELECTION:
        case kirpich::GameState::INIT_TYPE_C_DIFFICULTY:
        case kirpich::GameState::TYPE_C_LEVEL_SELECTION:
        case kirpich::GameState::TYPE_C_RISE_SELECTION:
            return true;
        case kirpich::GameState::ENTER_TOP_SCORE:
            // Name entry is looking at the difficulty screen it came from, heading and all, so the
            // heart belongs there for as long as that heading does. Every mode's name entry uses the
            // same screen, so this needs no fork on game type.
            return true;
        default:
            return false;
    }
}

std::optional<retropp::Sprite> heartIndicatorSprite(kirpich::GameState state, std::uint8_t heartMode,
                                                    const TileAtlas& atlas, std::uint8_t ramp) {
    if (!heartIndicatorShown(state, heartMode)) {
        return std::nullopt;
    }

    // One cell past the heading's last, on the heading's own row. Both come from the heading rather
    // than from numbers of this file's own, so moving the heading moves the heart with it.
    const int x = static_cast<int>(systems::kDifficultyHeadingCol + systems::kDifficultyHeadingCols) *
                      kCell +
                  kHeartIndicatorXOffset;
    const int y = static_cast<int>(systems::kDifficultyHeadingRow) * kCell + kHeartIndicatorYOffset;

    // The glyph the round's own panel draws. It sits at $27, which is the first tile of the block the
    // gameplay regime carries over from the copyright-and-title art - so it resolves to that art under
    // either regime, and the difficulty screens run under the gameplay one.
    const ResolvedTile art =
        resolveSpriteTile(static_cast<std::uint8_t>(kirpich::CharTile::HEART),
                          kirpich::TileSheet::GAMEPLAY, /*palette1=*/false, atlas, ramp);

    return retropp::Sprite{
        .key     = retropp::ObjectKey{"heart-mode"},
        .x       = x,
        .y       = y,
        .z       = kHeartZ,
        .atlas   = art.atlas,
        .tile    = art.cell,
        .palette = art.palette,
    };
}

}  // namespace kirpich::render
