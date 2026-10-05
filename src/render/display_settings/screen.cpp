#include "render/display_settings/screen.h"

#include <string>

#include "render/backdrop.h"
#include "render/background_layer.h"
#include "render/display_settings/layout.h"
#include "render/glyphs.h"
#include "render/option_row.h"
#include "render/sprite_layer.h"

namespace kirpich::render {

using namespace display_settings;

Layers DisplaySettingsScreen(const kirpich::DisplaySettingsState& ui, const kirpich::Settings& s,
                             bool blinkOn, const TileAtlas& atlas) {
    // The scales this build offers are all one digit, so the size reads as that digit and an x.
    const std::string scaleText = std::to_string(s.windowScale) + "x";

    return {
        BackgroundLayer("display-backdrop", 0, Backdrop(atlas, s.shadeRamp)),
        SpriteLayer("display-content", kContentZ, {
            Glyphs(kHeading, kHeadingX, kHeadingY, kPitch, atlas, s.shadeRamp),
            OptionRow("fullscreen", s.fullscreen ? "on" : "off", kFullscreenLine,
                      s.fullscreen, !s.fullscreen, atlas, s.shadeRamp),
            OptionRow("size", scaleText, kSizeLine,
                      s.windowScale > kirpich::kMinWindowScale,
                      s.windowScale < kirpich::kMaxWindowScale, atlas, s.shadeRamp),
            blinkOn ? Glyphs("-", kCursorX, cursorY(ui.row), kPitch, atlas, s.shadeRamp) : Sprites{},
        }),
    };
}

}  // namespace kirpich::render
