#include "render/controls/screen.h"

#include <array>
#include <string>
#include <string_view>

#include "render/backdrop.h"
#include "render/background_layer.h"
#include "render/controls/layout.h"
#include "render/controls/names.h"
#include "render/glyphs.h"
#include "render/sprite_layer.h"

namespace kirpich::render {

using namespace controls;

namespace {

// Each button's label, in GbButton order.
constexpr std::array<std::string_view, kirpich::kGbButtonCount> kButtonLabels{
    "up", "down", "left", "right", "a", "b", "start", "select",
};

// A run of text centered across the screen on its own line.
Sprites Centered(std::string_view text, int y, const TileAtlas& atlas, std::uint8_t ramp) {
    return Glyphs(text, optionHeadingX(text.size()), y, kPitch, atlas, ramp);
}

// The eight rows: each button's label, its key's name and its controller button's name. The cell the
// screen is waiting on reads kWaiting instead of its name.
Sprites ButtonRows(const kirpich::ControlsScreenState& ui, const kirpich::Controls& bindings,
                   retropp::ControllerType padFamily, const TileAtlas& atlas, std::uint8_t ramp) {
    Sprites rows;
    for (std::size_t i = 0; i < kirpich::kGbButtonCount; ++i) {
        const auto        button  = static_cast<kirpich::GbButton>(i);
        const int         y       = rowY(button);
        const bool        waiting = ui.listening && ui.row == button;
        const std::string key     = waiting && ui.column == kirpich::ControlsColumn::KEYBOARD
                                        ? std::string(kWaiting)
                                        : keyName(bindings[button].key);
        const std::string pad     = waiting && ui.column == kirpich::ControlsColumn::CONTROLLER
                                        ? std::string(kWaiting)
                                        : padButtonName(bindings[button].pad, padFamily);

        for (const Sprites& run : {Glyphs(kButtonLabels[i], kLabelX, y, kPitch, atlas, ramp),
                            Glyphs(key, kKeyNameX, y, kPitch, atlas, ramp),
                            Glyphs(pad, kControllerNameX, y, kPitch, atlas, ramp)}) {
            rows.insert(rows.end(), run.begin(), run.end());
        }
    }
    return rows;
}

}  // namespace

Layers ControlsScreen(const kirpich::ControlsScreenState& ui, const kirpich::Controls& bindings,
                      retropp::ControllerType padFamily, bool blinkOn, const TileAtlas& atlas,
                      std::uint8_t ramp) {
    return {
        BackgroundLayer("controls-backdrop", 0, Backdrop(atlas, ramp)),
        SpriteLayer("controls-content", kContentZ, {
            Glyphs(kHeading, kHeadingX, kHeadingY, kPitch, atlas, ramp),
            Glyphs("key", kKeyNameX, kColumnHeadY, kPitch, atlas, ramp),
            Glyphs("pad", kControllerNameX, kColumnHeadY, kPitch, atlas, ramp),
            ButtonRows(ui, bindings, padFamily, atlas, ramp),
            (blinkOn || ui.listening)
                ? Glyphs("-", cursorX(ui.column), rowY(ui.row), kPitch, atlas, ramp)
                : Sprites{},
            ui.listening ? Centered(prompt(ui.column), kPromptY, atlas, ramp) : Sprites{},
            ui.listening ? Centered(kCancelHint, kCancelHintY, atlas, ramp) : Sprites{},
        }),
    };
}

}  // namespace kirpich::render
