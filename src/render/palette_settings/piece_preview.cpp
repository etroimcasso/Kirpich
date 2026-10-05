#include "render/palette_settings/piece_preview.h"

#include <cstddef>
#include <string>

#include <kirpich/piece_kind.h>

#include "render/palette_settings/layout.h"
#include "render/piece_shape.h"

namespace kirpich::render {

using namespace palette_settings;

namespace {

// One row of the preview: the kinds from `first` up to `last`, exclusive, laid left to right from
// the x that centers them.
void addRow(Sprites& out, std::size_t first, std::size_t last, int y, const TileAtlas& atlas,
            std::uint8_t ramp) {
    int width = 0;
    for (std::size_t kind = first; kind < last; ++kind) {
        width += pieceShapeExtent(static_cast<kirpich::PieceKind>(kind)).width;
    }
    width += static_cast<int>(last - first - 1) * kPreviewGap;

    int x = (kScreenWidth - width) / 2;
    for (std::size_t kind = first; kind < last; ++kind) {
        const auto  piece = static_cast<kirpich::PieceKind>(kind);
        const Sprites shape =
            PieceShape(piece, x, y, kPieceZ, "palette-piece-" + std::to_string(kind), atlas, ramp);
        out.insert(out.end(), shape.begin(), shape.end());
        x += pieceShapeExtent(piece).width + kPreviewGap;
    }
}

}  // namespace

Sprites PiecePreview(const TileAtlas& atlas, std::uint8_t ramp) {
    Sprites out;
    addRow(out, 0, kPreviewFirstRowCount, kPreviewFirstRowY, atlas, ramp);
    addRow(out, kPreviewFirstRowCount, kirpich::kPieceKindCount, kPreviewSecondRowY, atlas, ramp);
    return out;
}

}  // namespace kirpich::render
