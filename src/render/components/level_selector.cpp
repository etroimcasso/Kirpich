#include "render/components/level_selector.h"

#include <string>

#include "render/components/arrow.h"
#include "render/components/glyphs.h"
#include "state/custom_rotations.h"  // kRotationLevelCount

namespace kirpich::render {

namespace {

constexpr int kGlyph = 8;

}  // namespace

static_assert(kLevelSelectorWidth == kLevelSelectorNumberDx + 2 * kGlyph,
              "the item ends at the right arrow's right edge");
static_assert(kLevelSelectorNumberDx >= static_cast<int>(std::string_view{"level"}.size()) * kGlyph +
                                            kGlyph,
              "the left arrow runs into the word");
static_assert(kirpich::kRotationLevelCount <= 10, "a level's number is one digit");

Sprites LevelSelector(std::string_view keyStem, std::uint8_t level, int x, int y,
                      const TileAtlas& atlas, std::uint8_t ramp) {
    const std::string stem{keyStem};
    const int         numberX = x + kLevelSelectorNumberDx;
    const bool        down    = level > 0;
    const bool        up      = level + 1 < kirpich::kRotationLevelCount;

    Sprites    out;
    const auto add = [&out](const Sprites& part) { out.insert(out.end(), part.begin(), part.end()); };
    add(Glyphs("level", x, y, kGlyph, atlas, ramp));
    add(Glyphs(std::to_string(level), numberX, y, kGlyph, atlas, ramp));
    if (down) {
        add(Arrow(stem + "-left", numberX - kGlyph, y, ArrowDirection::LEFT, kLevelSelectorArrowZ,
                  atlas, ramp));
        add(Arrow(stem + "-down", numberX, y + kGlyph, ArrowDirection::DOWN, kLevelSelectorArrowZ,
                  atlas, ramp));
    }
    if (up) {
        add(Arrow(stem + "-right", numberX + kGlyph, y, ArrowDirection::RIGHT, kLevelSelectorArrowZ,
                  atlas, ramp));
        add(Arrow(stem + "-up", numberX, y - kGlyph, ArrowDirection::UP, kLevelSelectorArrowZ, atlas,
                  ramp));
    }
    return out;
}

}  // namespace kirpich::render
