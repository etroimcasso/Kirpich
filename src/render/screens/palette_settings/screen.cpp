#include "render/screens/palette_settings/screen.h"

#include <string>

#include "render/components/backdrop.h"
#include "render/components/background_layer.h"
#include "render/components/glyphs.h"
#include "render/components/option_row.h"
#include "render/components/page_arrows.h"
#include "render/components/piece_preview.h"
#include "render/components/sprite_layer.h"
#include "render/palettes.h"                  // kShadeRampCount
#include "render/screens/palette_settings/layout.h"
#include "render/screens/palette_settings/ramp_swatch.h"
#include "systems/palette_settings_screen.h"  // paletteCanStep
#include "systems/palette_walk.h"             // paletteChoiceOf, paletteChoiceText

namespace kirpich::render {

using namespace palette_settings;

// The number is counted from one and takes one or two cells; a slot takes the C and two digits, which
// is the whole value field.
static_assert(kShadeRampCount <= 99,
              "a palette past the ninety-ninth has no room in the two cells its number is drawn in");
static_assert(kirpich::kCustomPaletteSlotCount <= 99,
              "a slot past the ninety-ninth has no room in the two cells after the C");

Layers PaletteSettingsScreen(const kirpich::Settings& s, const kirpich::CustomPalettes& palettes,
                             bool blinkOn, const TileAtlas& atlas) {
    const std::uint8_t index = kirpich::paletteIndexOf(s);
    // The ramp's number, `C` and the saved slot as two digits, or `C` alone.
    const std::string value =
        systems::paletteChoiceText(systems::paletteChoiceOf(s, &palettes));

    return {
        BackgroundLayer("palette-backdrop", 0, Backdrop(atlas, index)),
        SpriteLayer("palette-content", kContentZ, {
            Glyphs(kHeading, kHeadingX, kHeadingY, kPitch, atlas, index),
            OptionRow("palette", value, kPaletteLine,
                      systems::paletteCanStep(s, &palettes, -1),
                      systems::paletteCanStep(s, &palettes, +1), atlas, index),
            blinkOn ? Glyphs("-", kCursorX, kCursorY, kPitch, atlas, index) : Sprites{},
            PiecePreview("palette-piece", atlas, index),
            PageArrows("palette", /*up=*/true, /*down=*/true, atlas, index),
        }, LayerOptions{.regions = RampSwatch(atlas, index)}),
    };
}

}  // namespace kirpich::render
