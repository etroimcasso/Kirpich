#include "render/components/option_row.h"

#include "render/components/glyphs.h"
#include "render/components/scroller_arrows.h"

namespace kirpich::render {

Sprites OptionRow(std::string_view label, std::string_view value, std::size_t line, bool left,
                  bool right, const TileAtlas& atlas, std::uint8_t ramp) {
    const int y = optionPixels(line);

    Sprites    out  = Glyphs(label, optionPixels(systems::kLabelCol), y, kOptionCell, atlas, ramp);
    const auto add  = [&out](const Sprites& part) { out.insert(out.end(), part.begin(), part.end()); };

    add(Glyphs(value, optionPixels(systems::kOptionValueCol), y, kOptionCell, atlas, ramp));
    add(ScrollerArrows(line, left, right, atlas, ramp));
    return out;
}

}  // namespace kirpich::render
