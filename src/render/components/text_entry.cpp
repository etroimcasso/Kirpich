#include "render/components/text_entry.h"

#include <string>

#include "render/components/arrow.h"
#include "render/components/glyphs.h"
#include "render/components/sprite_layer.h"

namespace kirpich::render {

namespace {

// The arrows stand above the glyphs inside the layer.
constexpr std::int32_t kArrowZ = 30;

// The run, one place at a time, so the place being changed can blink alone.
Sprites Places(std::string_view text, std::size_t place, bool blinkOn, const TileAtlas& atlas,
               std::uint8_t ramp) {
    const int x = textEntryTextX(text.size());
    Sprites   out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == place && !blinkOn) continue;
        const Sprites glyph = SelectedGlyphs(text.substr(i, 1), x + static_cast<int>(i) * kTextEntryGlyph,
                                             kTextEntryTextY, kTextEntryGlyph, atlas, ramp);
        out.insert(out.end(), glyph.begin(), glyph.end());
    }
    return out;
}

}  // namespace

retropp::DrawLayer TextEntry(std::string_view key, std::string_view title, std::string_view text,
                             std::size_t place, bool blinkOn, const TileAtlas& atlas,
                             std::uint8_t ramp) {
    const int placeX =
        textEntryTextX(text.size()) + static_cast<int>(place) * kTextEntryGlyph;
    const std::string name{key};
    return SpriteLayer(key, kTextEntryZ, {
        Glyphs(title, textEntryTextX(title.size()), kTextEntryTitleY, kTextEntryGlyph, atlas, ramp),
        Places(text, place, blinkOn, atlas, ramp),
        Arrow(name + "-up", placeX, kTextEntryTextY - kTextEntryArrowRise, ArrowDirection::UP, kArrowZ,
              atlas, ramp),
        Arrow(name + "-down", placeX, kTextEntryTextY + kTextEntryArrowRise, ArrowDirection::DOWN,
              kArrowZ, atlas, ramp),
    });
}

}  // namespace kirpich::render
