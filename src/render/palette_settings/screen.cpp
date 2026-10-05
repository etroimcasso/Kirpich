#include "render/palette_settings/screen.h"

#include <string>

#include "render/backdrop.h"
#include "render/background_layer.h"
#include "render/glyphs.h"
#include "render/option_row.h"
#include "render/palette_settings/layout.h"
#include "render/palette_settings/piece_preview.h"
#include "render/palette_settings/ramp_swatch.h"
#include "render/palettes.h"  // kShadeRampCount, clampShadeRamp
#include "render/sprite_layer.h"

namespace kirpich::render {

using namespace palette_settings;

// The number is counted from one and takes one or two cells.
static_assert(kShadeRampCount <= 99,
              "a palette past the ninety-ninth has no room in the two cells its number is drawn in");

Layers PaletteSettingsScreen(const kirpich::Settings& s, bool blinkOn, const TileAtlas& atlas) {
    const std::uint8_t ramp   = clampShadeRamp(s.shadeRamp);
    const std::string  number = std::to_string(ramp + 1);

    return {
        BackgroundLayer("palette-backdrop", 0, Backdrop(atlas, ramp)),
        SpriteLayer("palette-content", kContentZ, {
            Glyphs(kHeading, kHeadingX, kHeadingY, kPitch, atlas, ramp),
            OptionRow("palette", number, kPaletteLine, ramp > 0, ramp + 1 < kShadeRampCount,
                      atlas, ramp),
            blinkOn ? Glyphs("-", kCursorX, kCursorY, kPitch, atlas, ramp) : Sprites{},
            PiecePreview(atlas, ramp),
        }, LayerOptions{.regions = RampSwatch(ramp)}),
    };
}

}  // namespace kirpich::render
