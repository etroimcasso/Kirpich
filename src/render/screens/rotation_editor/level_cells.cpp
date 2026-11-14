#include "render/screens/rotation_editor/level_cells.h"

#include <cstddef>
#include <string>

#include "render/components/arrow.h"
#include "render/components/glyphs.h"
#include "render/screens/rotation_editor/layout.h"
#include "render/screens/rotation_editor/rotation_editor.h"  // levelPaletteChoice, enteredNumberCanStep
#include "systems/palette_walk.h"                            // paletteChoiceText

namespace kirpich::render {

using namespace rotation_editor;

Sprites LevelCells(const kirpich::RotationEditorState& ui, const kirpich::CustomRotations& rotations,
                   const kirpich::CustomPalettes& palettes, bool blinkOn, const TileAtlas& atlas,
                   std::uint8_t ramp) {
    Sprites    out;
    const auto add = [&out](const Sprites& part) { out.insert(out.end(), part.begin(), part.end()); };

    for (std::size_t level = 0; level < kirpich::kRotationLevelCount; ++level) {
        const int x = cellX(level);
        const int y = cellY(level);

        add(Glyphs(std::to_string(level), x + kCellLevelDx, y, kGlyph, atlas, ramp));
        add(Glyphs(":", x + kCellColonDx, y, kGlyph, atlas, ramp));

        const std::string number = systems::paletteChoiceText(
            systems::levelPaletteChoice(ui, rotations, &palettes, level));
        const bool entered = ui.entering && ui.level == level;
        add(entered ? SelectedGlyphs(number, x + kCellNumberDx, y, kGlyph, atlas, ramp)
                    : Glyphs(number, x + kCellNumberDx, y, kGlyph, atlas, ramp));

        if (entered && systems::enteredNumberCanStep(ui, &palettes, -1)) {
            add(Arrow("rotation-editor-number-left", x + kCellLeftArrowDx, y, ArrowDirection::LEFT,
                      kArrowZ, atlas, ramp));
        }
        if (entered && systems::enteredNumberCanStep(ui, &palettes, +1)) {
            add(Arrow("rotation-editor-number-right", x + kCellRightArrowDx, y,
                      ArrowDirection::RIGHT, kArrowZ, atlas, ramp));
        }
    }

    if (ui.row == kirpich::RotationEditorRow::LEVELS && !ui.entering && blinkOn) {
        add(Glyphs("-", cellX(ui.level) + kCellCursorDx, cellY(ui.level), kGlyph, atlas, ramp));
    }
    return out;
}

}  // namespace kirpich::render
