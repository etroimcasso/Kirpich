#include "render/screens/display_settings/screen.h"

#include <string>

#include "render/components/backdrop.h"
#include "render/components/background_layer.h"
#include "render/components/glyphs.h"
#include "render/components/option_row.h"
#include "render/components/sprite_layer.h"
#include "render/screens/display_settings/layout.h"

namespace kirpich::render {

using namespace display_settings;

Layers DisplaySettingsScreen(const kirpich::DisplaySettingsState& ui, const kirpich::Settings& s,
                             bool blinkOn, const TileAtlas& atlas) {
    // The scales this build offers are all one digit, so the size reads as that digit and an x.
    const std::string scaleText = std::to_string(s.windowScale) + "x";

    // The palette the game is drawn in: the custom set while a custom palette is in use.
    const std::uint8_t index = kirpich::paletteIndexOf(s);

    return {
        BackgroundLayer("display-backdrop", 0, Backdrop(atlas, index)),
        SpriteLayer("display-content", kContentZ, {
            Glyphs(kHeading, kHeadingX, kHeadingY, kPitch, atlas, index),
            OptionRow("fullscreen", s.fullscreen ? "on" : "off", kFullscreenLine,
                      s.fullscreen, !s.fullscreen, atlas, index),
            OptionRow("size", scaleText, kSizeLine,
                      s.windowScale > kirpich::kMinWindowScale,
                      s.windowScale < kirpich::kMaxWindowScale, atlas, index),
            blinkOn ? Glyphs("-", kCursorX, cursorY(ui.row), kPitch, atlas, index) : Sprites{},
        }),
    };
}

}  // namespace kirpich::render
