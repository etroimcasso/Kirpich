#include "render/screens/palette_settings/screen.h"

#include <optional>
#include <string>

#include "render/components/backdrop.h"
#include "render/components/background_layer.h"
#include "render/components/glyphs.h"
#include "render/components/option_row.h"
#include "render/components/page_arrows.h"
#include "render/components/sprite_layer.h"
#include "render/palettes.h"                  // kShadeRampCount
#include "render/screens/palette_settings/layout.h"
#include "render/screens/palette_settings/piece_preview.h"
#include "render/screens/palette_settings/ramp_swatch.h"
#include "systems/palette_settings_screen.h"  // paletteCanStep

namespace kirpich::render {

using namespace palette_settings;

// The number is counted from one and takes one or two cells; a slot takes the C and two digits, which
// is the whole value field.
static_assert(kShadeRampCount <= 99,
              "a palette past the ninety-ninth has no room in the two cells its number is drawn in");
static_assert(kirpich::kCustomPaletteSlotCount <= 99,
              "a slot past the ninety-ninth has no room in the two cells after the C");

namespace {

// What the selector reads: the ramp's number, `C` and the saved slot as two digits, or `C` alone.
std::string paletteValueText(const kirpich::Settings& s, const kirpich::CustomPalettes& palettes) {
    if (!s.customPalette) {
        return std::to_string(clampShadeRamp(s.shadeRamp) + 1);
    }
    const std::optional<std::uint8_t> slot = kirpich::savedSlotOf(palettes);
    if (!slot) {
        return "C";
    }
    return (*slot < 10 ? "C0" : "C") + std::to_string(*slot);
}

}  // namespace

Layers PaletteSettingsScreen(const kirpich::Settings& s, const kirpich::CustomPalettes& palettes,
                             bool blinkOn, const TileAtlas& atlas) {
    const std::uint8_t index = kirpich::paletteIndexOf(s);
    const std::string  value = paletteValueText(s, palettes);

    return {
        BackgroundLayer("palette-backdrop", 0, Backdrop(atlas, index)),
        SpriteLayer("palette-content", kContentZ, {
            Glyphs(kHeading, kHeadingX, kHeadingY, kPitch, atlas, index),
            OptionRow("palette", value, kPaletteLine,
                      systems::paletteCanStep(s, &palettes, -1),
                      systems::paletteCanStep(s, &palettes, +1), atlas, index),
            blinkOn ? Glyphs("-", kCursorX, kCursorY, kPitch, atlas, index) : Sprites{},
            PiecePreview(atlas, index),
            PageArrows("palette", /*up=*/false, /*down=*/true, atlas, index),
        }, LayerOptions{.regions = RampSwatch(atlas, index)}),
    };
}

}  // namespace kirpich::render
