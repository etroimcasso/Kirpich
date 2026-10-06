#include "render/controls/restore_confirm.h"

#include <cstddef>

#include "render/controls/layout.h"
#include "render/glyphs.h"
#include "render/option_row.h"
#include "render/sprite_layer.h"
#include "systems/settings_screen.h"

namespace kirpich::render {

using namespace controls;

namespace {

constexpr systems::ConfirmChoiceColumns kChoices =
    systems::confirmChoiceColumns(kConfirmNo.size(), kConfirmYes.size());

constexpr int kFirstLineY  = optionPixels(systems::kConfirmQuestionFirstRow);
constexpr int kSecondLineY = optionPixels(systems::kConfirmQuestionSecondRow);
constexpr int kChoiceY     = optionPixels(systems::kConfirmChoiceRow);

}  // namespace

retropp::DrawLayer RestoreConfirm(bool yes, bool blinkOn, const TileAtlas& atlas,
                                  std::uint8_t ramp) {
    const std::size_t cursorCol = (yes ? kChoices.right : kChoices.left) - systems::kConfirmCursorGap;

    return SpriteLayer("controls-confirm", kContentZ, {
        Glyphs(kRestoreLabel, optionHeadingX(kRestoreLabel.size()), kHeadingY, kPitch, atlas, ramp),
        Glyphs(kConfirmFirstLine, optionHeadingX(kConfirmFirstLine.size()), kFirstLineY, kPitch,
               atlas, ramp),
        Glyphs(kConfirmSecondLine, optionHeadingX(kConfirmSecondLine.size()), kSecondLineY, kPitch,
               atlas, ramp),
        Glyphs(kConfirmNo, optionPixels(kChoices.left), kChoiceY, kPitch, atlas, ramp),
        Glyphs(kConfirmYes, optionPixels(kChoices.right), kChoiceY, kPitch, atlas, ramp),
        blinkOn ? Glyphs("-", optionPixels(cursorCol), kChoiceY, kPitch, atlas, ramp) : Sprites{},
    });
}

}  // namespace kirpich::render
