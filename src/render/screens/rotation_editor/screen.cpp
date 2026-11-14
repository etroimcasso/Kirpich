#include "render/screens/rotation_editor/screen.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "render/components/arrow.h"
#include "render/components/backdrop.h"
#include "render/components/background_layer.h"
#include "render/components/glyphs.h"
#include "render/components/palette_preview.h"
#include "render/components/sprite_layer.h"
#include "render/components/text_entry.h"
#include "render/components/yes_no_question.h"
#include "render/palettes.h"  // kRotationRamp
#include "render/screens/rotation_editor/layout.h"
#include "render/screens/rotation_editor/level_cell_swatches.h"
#include "render/screens/rotation_editor/level_cells.h"
#include "render/screens/rotation_editor/level_preview_backdrop.h"
#include "render/screens/rotation_editor/rotation_editor.h"  // the logic's own answers

namespace kirpich::render {

using namespace rotation_editor;

namespace {

using kirpich::RotationEditorQuestion;
using kirpich::RotationEditorRow;
using kirpich::systems::RotationChoice;

[[nodiscard]] int rowY(RotationEditorRow row) noexcept {
    switch (row) {
        case RotationEditorRow::LOAD:
            return kLoadY;
        case RotationEditorRow::SAVE:
            return kSaveY;
        case RotationEditorRow::CLEAR:
            return kClearY;
        default:
            break;
    }
    return kPresetY;
}

// What each question says. The second line names the preset as two digits where it is about a slot.
std::string_view questionTitle(RotationEditorQuestion q) noexcept {
    switch (q) {
        case RotationEditorQuestion::LOAD_UNSAVED:
            return "load preset";
        case RotationEditorQuestion::SAVE_OVER:
            return "save preset";
        default:
            break;
    }
    return "clear preset";
}

std::string_view questionFirstLine(RotationEditorQuestion q) noexcept {
    switch (q) {
        case RotationEditorQuestion::LOAD_UNSAVED:
            return "your rotation is";
        case RotationEditorQuestion::SAVE_OVER:
            return "replace";
        default:
            break;
    }
    return "empty";
}

std::string questionSecondLine(RotationEditorQuestion q, RotationChoice preset) {
    if (q == RotationEditorQuestion::LOAD_UNSAVED) {
        return "not saved";
    }
    return (preset.slot < 10 ? "preset 0" : "preset ") + std::to_string(preset.slot);
}

// The heading, and the asterisk after it while the working rotation has unsaved changes.
Sprites Heading(const kirpich::CustomRotations& rotations, const TileAtlas& atlas,
                std::uint8_t ramp) {
    Sprites out = Glyphs(kHeading, kHeadingX, kHeadingY, kGlyph, atlas, ramp);
    if (kirpich::hasUnsavedChanges(rotations)) {
        const Sprites asterisk = Glyphs("*", kAsteriskX, kHeadingY, kGlyph, atlas, ramp);
        out.insert(out.end(), asterisk.begin(), asterisk.end());
    }
    return out;
}

// What the preset selector reads, dimmed while it names an empty slot, so an unused slot reads as one
// without consulting the actions below it.
Sprites PresetValue(RotationChoice preset, const kirpich::CustomRotations& rotations,
                    const TileAtlas& atlas, std::uint8_t ramp) {
    const std::string text  = systems::rotationPresetText(preset, rotations);
    const bool        empty = preset.mode == kirpich::PaletteRotation::CUSTOM &&
                       (preset.slot < 1 || preset.slot > kirpich::kCustomRotationSlotCount ||
                        !rotations.slots[preset.slot - 1].present);
    return empty ? DimGlyphs(text, kPresetValueX, kPresetY, kGlyph, atlas, ramp)
                 : Glyphs(text, kPresetValueX, kPresetY, kGlyph, atlas, ramp);
}

// One action's label, dimmed while the action can do nothing.
Sprites ActionLabel(std::string_view label, RotationEditorRow action, RotationChoice preset,
                    const kirpich::CustomRotations& rotations, const TileAtlas& atlas,
                    std::uint8_t ramp) {
    const int y = rowY(action);
    return systems::rotationEditorActionCanAct(action, preset, &rotations)
               ? Glyphs(label, kLabelX, y, kGlyph, atlas, ramp)
               : DimGlyphs(label, kLabelX, y, kGlyph, atlas, ramp);
}

// The row cursor on the four rows above the grid. The grid's own cursor is LevelCells'.
Sprites RowCursor(const kirpich::RotationEditorState& ui, bool blinkOn, const TileAtlas& atlas,
                  std::uint8_t ramp) {
    if (!blinkOn || ui.entering || ui.row == RotationEditorRow::LEVELS) {
        return {};
    }
    return Glyphs("-", kCursorX, rowY(ui.row), kGlyph, atlas, ramp);
}

// The held preview: the words and the seven pieces, through the rotation set.
retropp::DrawLayer Preview(const TileAtlas& atlas) {
    return SpriteLayer("rotation-editor-preview", kContentZ, {
        PalettePreview(atlas, kRotationRamp),
    });
}

retropp::DrawLayer Editor(const kirpich::RotationEditorState& ui,
                          const kirpich::CustomRotations&     rotations,
                          const kirpich::CustomPalettes& palettes, bool blinkOn,
                          const TileAtlas& atlas, std::uint8_t ramp) {
    return SpriteLayer("rotation-editor-content", kContentZ, {
        Heading(rotations, atlas, ramp),
        Glyphs("preset", kLabelX, kPresetY, kGlyph, atlas, ramp),
        PresetValue(ui.preset, rotations, atlas, ramp),
        systems::rotationEditorPresetCanStep(ui.preset, &rotations, -1)
            ? Arrow("rotation-editor-preset-left", kPresetLeftArrowX, kPresetY,
                    ArrowDirection::LEFT, kArrowZ, atlas, ramp)
            : Sprites{},
        systems::rotationEditorPresetCanStep(ui.preset, &rotations, +1)
            ? Arrow("rotation-editor-preset-right", kPresetRightArrowX, kPresetY,
                    ArrowDirection::RIGHT, kArrowZ, atlas, ramp)
            : Sprites{},
        ActionLabel("load preset", RotationEditorRow::LOAD, ui.preset, rotations, atlas, ramp),
        ActionLabel("save preset", RotationEditorRow::SAVE, ui.preset, rotations, atlas, ramp),
        ActionLabel("clear preset", RotationEditorRow::CLEAR, ui.preset, rotations, atlas, ramp),
        RowCursor(ui, blinkOn, atlas, ramp),
        LevelCells(ui, rotations, palettes, blinkOn, atlas, ramp),
    }, LayerOptions{.regions = LevelCellSwatches(rotations.working)});
}

}  // namespace

Layers RotationEditorScreen(const kirpich::RotationEditorState& ui, const kirpich::Settings& s,
                            const kirpich::CustomRotations& rotations,
                            const kirpich::CustomPalettes& palettes, bool blinkOn,
                            const TileAtlas& atlas) {
    // The editor is a menu and draws through the palette the player chose. The held preview is the one
    // part drawn through the rotation set, which holds the working rotation's palette for the selected
    // level while the preview is up (systems/frame_palette.h) - whichever rotation is switched on.
    const std::uint8_t ramp = kirpich::paletteIndexOf(s);
    return {
        ui.previewing
            ? BackgroundLayer("rotation-editor-backdrop", 0,
                              LevelPreviewBackdrop(ui.level, kPreviewLevelBoxColumn,
                                                   kPreviewLevelBoxRow, atlas, kRotationRamp))
            : BackgroundLayer("rotation-editor-backdrop", 0, Backdrop(atlas, ramp)),
        ui.previewing ? Preview(atlas)
        : ui.naming
            ? TextEntry("rotation-editor-name", kNameTitle,
                        std::string_view(ui.typed.data(), ui.typed.size()), ui.namePlace, blinkOn,
                        atlas, ramp)
        : ui.question != RotationEditorQuestion::NONE
            ? YesNoQuestion("rotation-editor-question", questionTitle(ui.question),
                            questionFirstLine(ui.question),
                            questionSecondLine(ui.question, ui.preset), ui.answerYes, blinkOn,
                            atlas, ramp)
            : Editor(ui, rotations, palettes, blinkOn, atlas, ramp),
    };
}

}  // namespace kirpich::render
