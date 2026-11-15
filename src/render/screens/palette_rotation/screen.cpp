#include "render/screens/palette_rotation/screen.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "render/components/arrow.h"
#include "render/components/backdrop.h"
#include "render/components/background_layer.h"
#include "render/components/glyphs.h"
#include "render/components/page_arrows.h"
#include "render/components/palette_preview.h"
#include "render/components/preview_backdrop.h"
#include "render/components/sprite_layer.h"
#include "render/palettes.h"          // kRotationRamp
#include "render/rotation_presets.h"  // presetRotationName
#include "render/screens/palette_rotation/layout.h"
#include "render/screens/palette_rotation/level_grid_digits.h"
#include "render/screens/palette_rotation/level_grid_swatches.h"
#include "render/screens/palette_rotation/rotation_screen.h"  // rotationCanStep
#include "systems/frame_palette.h"                            // rotationPalettes

namespace kirpich::render {

using namespace palette_rotation;

static_assert(kirpich::kCustomRotationSlotCount <= 99,
              "a slot past the ninety-ninth has no room in the two digits after the C");

namespace {

using kirpich::PaletteRotation;
using kirpich::RotationScreenRow;

[[nodiscard]] int rowY(RotationScreenRow row) noexcept {
    return row == RotationScreenRow::EDITOR ? kEditorY : kSelectorY;
}

// The word the selector reads for `off` and for each preset; nothing for CUSTOM, which reads its slot.
[[nodiscard]] constexpr std::string_view modeName(PaletteRotation mode) noexcept {
    return mode == PaletteRotation::OFF ? std::string_view{"off"} : presetRotationName(mode);
}

// What the selector reads: the mode's name; for CUSTOM, the name of the slot the working rotation is
// saved in, or `C` and the slot as two digits where that slot has no name, or `C` alone where it is
// saved in none.
std::string rotationValueText(const kirpich::Settings& s, const kirpich::CustomRotations& rotations) {
    if (s.paletteRotation != PaletteRotation::CUSTOM) {
        return std::string{modeName(s.paletteRotation)};
    }
    const std::optional<std::uint8_t> slot = kirpich::savedSlotOf(rotations);
    if (!slot) {
        return "C";
    }
    const std::string name = kirpich::rotationNameText(rotations.slots[*slot - 1].name);
    if (!name.empty()) {
        return name;
    }
    return (*slot < 10 ? "C0" : "C") + std::to_string(*slot);
}

// The item that opens the rotation editor.
Sprites EditorItem(const TileAtlas& atlas, std::uint8_t ramp) {
    return Glyphs(kEditorItem, kLabelX, kEditorY, kGlyph, atlas, ramp);
}

// The colors the grid shows: the mode's ten palettes, or the player's own palette at every level while
// the mode is OFF.
LevelColors levelColors(const kirpich::Settings& s, const kirpich::CustomRotations& rotations,
                        const TileAtlas& atlas) {
    LevelColors out{};
    if (const RotationPalettes* palettes = systems::rotationPalettes(s, rotations.working)) {
        for (std::size_t level = 0; level < out.size(); ++level) {
            const ShadeRamp& p = (*palettes)[level];
            out[level]         = {p.darkest, p.dark, p.light, p.lightest};
        }
        return out;
    }
    out.fill(shadesOf(atlas, kirpich::paletteIndexOf(s)));
    return out;
}

retropp::DrawLayer Menu(const kirpich::RotationScreenState& ui, const kirpich::Settings& s,
                        const kirpich::CustomRotations& rotations, bool blinkOn,
                        const TileAtlas& atlas, std::uint8_t ramp) {
    return SpriteLayer("rotation-content", kContentZ, {
        Glyphs(kHeading, kHeadingX, kHeadingY, kGlyph, atlas, ramp),
        LevelGridDigits(atlas, ramp),
        EditorItem(atlas, ramp),
        Glyphs(kSelectorLabel, kLabelX, kSelectorY, kGlyph, atlas, ramp),
        Glyphs(rotationValueText(s, rotations), kSelectorValueX, kSelectorY, kGlyph, atlas, ramp),
        systems::rotationCanStep(s, &rotations, -1)
            ? Arrow("rotation-left", kSelectorLeftArrowX, kSelectorY, ArrowDirection::LEFT, kArrowZ,
                    atlas, ramp)
            : Sprites{},
        systems::rotationCanStep(s, &rotations, +1)
            ? Arrow("rotation-right", kSelectorRightArrowX, kSelectorY, ArrowDirection::RIGHT,
                    kArrowZ, atlas, ramp)
            : Sprites{},
        blinkOn ? Glyphs("-", kCursorX, rowY(ui.row), kGlyph, atlas, ramp) : Sprites{},
        PageArrows("rotation", /*up=*/false, /*down=*/true, atlas, ramp),
    }, LayerOptions{.regions = LevelGridSwatches(levelColors(s, rotations, atlas))});
}

// The held preview: the heading, then the words, the pieces and the `level` item, all through the
// level's palette.
retropp::DrawLayer Preview(std::uint8_t level, const TileAtlas& atlas, std::uint8_t preview) {
    return SpriteLayer("rotation-preview", kContentZ, {
        Glyphs(kHeading, kHeadingX, kHeadingY, kGlyph, atlas, preview),
        PalettePreview(atlas, preview, level),
    });
}

}  // namespace

Layers PaletteRotationScreen(const kirpich::RotationScreenState& ui, const kirpich::Settings& s,
                             const kirpich::CustomRotations& rotations, bool blinkOn,
                             const TileAtlas& atlas) {
    // The screen is a menu and draws through the palette the player chose, as every screen outside a
    // round does. The held preview is the one part drawn through the level's palette: the rotation set
    // while a rotation is on, the player's own palette while it is off - which is what every level
    // looks like then.
    const std::uint8_t ramp    = kirpich::paletteIndexOf(s);
    const std::uint8_t preview = s.paletteRotation == PaletteRotation::OFF ? ramp : kRotationRamp;
    return {
        ui.previewing ? BackgroundLayer("rotation-backdrop", 0, PreviewBackdrop(atlas, preview))
                      : BackgroundLayer("rotation-backdrop", 0, Backdrop(atlas, ramp)),
        ui.previewing ? Preview(ui.previewLevel, atlas, preview)
                      : Menu(ui, s, rotations, blinkOn, atlas, ramp),
    };
}

}  // namespace kirpich::render
