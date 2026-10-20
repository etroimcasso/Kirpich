#include "render/components/yes_no_question.h"

#include <cstddef>

#include "render/components/glyphs.h"
#include "render/components/option_row.h"  // optionPixels, optionHeadingX, kOptionHeadingY, kOptionCell
#include "render/components/sprite_layer.h"
#include "systems/settings_screen.h"       // the confirm's rows and columns

namespace kirpich::render {

namespace {

constexpr systems::ConfirmChoiceColumns kChoices =
    systems::confirmChoiceColumns(kQuestionNo.size(), kQuestionYes.size());

constexpr int kFirstLineY  = optionPixels(systems::kConfirmQuestionFirstRow);
constexpr int kSecondLineY = optionPixels(systems::kConfirmQuestionSecondRow);
constexpr int kChoiceY     = optionPixels(systems::kConfirmChoiceRow);

}  // namespace

retropp::DrawLayer YesNoQuestion(std::string_view key, std::string_view title,
                                 std::string_view firstLine, std::string_view secondLine, bool yes,
                                 bool blinkOn, const TileAtlas& atlas, std::uint8_t ramp) {
    const std::size_t cursorCol = (yes ? kChoices.right : kChoices.left) - systems::kConfirmCursorGap;

    return SpriteLayer(key, kQuestionZ, {
        Glyphs(title, optionHeadingX(title.size()), kOptionHeadingY, kOptionCell, atlas, ramp),
        Glyphs(firstLine, optionHeadingX(firstLine.size()), kFirstLineY, kOptionCell, atlas, ramp),
        Glyphs(secondLine, optionHeadingX(secondLine.size()), kSecondLineY, kOptionCell, atlas,
               ramp),
        Glyphs(kQuestionNo, optionPixels(kChoices.left), kChoiceY, kOptionCell, atlas, ramp),
        Glyphs(kQuestionYes, optionPixels(kChoices.right), kChoiceY, kOptionCell, atlas, ramp),
        blinkOn ? Glyphs("-", optionPixels(cursorCol), kChoiceY, kOptionCell, atlas, ramp)
                : Sprites{},
    });
}

}  // namespace kirpich::render
