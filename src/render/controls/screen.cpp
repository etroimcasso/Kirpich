#include "render/controls/screen.h"

#include <array>
#include <string>
#include <string_view>

#include "render/backdrop.h"
#include "render/background_layer.h"
#include "render/controls/layout.h"
#include "render/controls/names.h"
#include "render/controls/restore_confirm.h"
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
        const bool        waiting = ui.listening && ui.row == kirpich::rowOf(button);
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
    // The restore question takes the whole screen while it is asked, as the settings confirms do.
    const bool asking = ui.confirmingRestore;

    return {
        BackgroundLayer("controls-backdrop", 0, Backdrop(atlas, ramp)),
        SpriteLayer("controls-content", kContentZ, {
            asking ? RestoreConfirm(ui.confirmYes, blinkOn, atlas, ramp) : Sprites{},
            asking ? Sprites{} : Glyphs(kHeading, kHeadingX, kHeadingY, kPitch, atlas, ramp),
            asking ? Sprites{} : Glyphs("key", kKeyNameX, kColumnHeadY, kPitch, atlas, ramp),
            asking ? Sprites{} : Glyphs("pad", kControllerNameX, kColumnHeadY, kPitch, atlas, ramp),
            asking ? Sprites{} : ButtonRows(ui, bindings, padFamily, atlas, ramp),
            asking ? Sprites{}
                   : Glyphs(kRestoreLabel, kRestoreLabelX,
                            rowY(kirpich::ControlsRow::RESTORE_DEFAULTS), kPitch, atlas, ramp),
            !asking && (blinkOn || ui.listening)
                ? Glyphs("-", cursorX(ui.row, ui.column), rowY(ui.row), kPitch, atlas, ramp)
                : Sprites{},
            ui.listening ? Centered(prompt(ui.column), kPromptY, atlas, ramp) : Sprites{},
            ui.listening ? Centered(kCancelHint, kCancelHintY, atlas, ramp) : Sprites{},
        }),
    };
}

}  // namespace kirpich::render
