#include "render/screens/controls/restore_confirm.h"

#include "render/components/yes_no_question.h"
#include "render/screens/controls/layout.h"

namespace kirpich::render {

using namespace controls;

retropp::DrawLayer RestoreConfirm(bool yes, bool blinkOn, const TileAtlas& atlas,
                                  std::uint8_t ramp) {
    return YesNoQuestion("controls-confirm", kRestoreLabel, kConfirmFirstLine, kConfirmSecondLine,
                         yes, blinkOn, atlas, ramp);
}

}  // namespace kirpich::render
