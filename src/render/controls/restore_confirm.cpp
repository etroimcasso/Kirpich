#include "render/controls/restore_confirm.h"

#include <string_view>

#include "render/controls/layout.h"
#include "render/glyphs.h"
#include "render/option_row.h"
#include "systems/settings_screen.h"

namespace kirpich::render {

using namespace controls;

Sprites RestoreConfirm(bool yes, bool blinkOn, const TileAtlas& atlas, std::uint8_t ramp) {
    const systems::ConfirmChoiceColumns cols =
        systems::confirmChoiceColumns(kConfirmNo.size(), kConfirmYes.size());
    const std::size_t cursorCol = (yes ? cols.right : cols.left) - systems::kConfirmCursorGap;

    Sprites sprites;
    for (const Sprites& run :
         {Glyphs(kRestoreLabel, optionHeadingX(kRestoreLabel.size()), kHeadingY, kPitch, atlas, ramp),
          Glyphs(kConfirmFirstLine, optionHeadingX(kConfirmFirstLine.size()),
                 optionPixels(systems::kConfirmQuestionFirstRow), kPitch, atlas, ramp),
          Glyphs(kConfirmSecondLine, optionHeadingX(kConfirmSecondLine.size()),
                 optionPixels(systems::kConfirmQuestionSecondRow), kPitch, atlas, ramp),
          Glyphs(kConfirmNo, optionPixels(cols.left), optionPixels(systems::kConfirmChoiceRow),
                 kPitch, atlas, ramp),
          Glyphs(kConfirmYes, optionPixels(cols.right), optionPixels(systems::kConfirmChoiceRow),
                 kPitch, atlas, ramp),
          blinkOn ? Glyphs("-", optionPixels(cursorCol), optionPixels(systems::kConfirmChoiceRow),
                           kPitch, atlas, ramp)
                  : Sprites{}}) {
        sprites.insert(sprites.end(), run.begin(), run.end());
    }
    return sprites;
}

}  // namespace kirpich::render
