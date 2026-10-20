#include "render/components/piece_preview.h"

#include <cstddef>
#include <string>

#include <kirpich/piece_kind.h>

#include "render/components/piece_shape.h"

namespace kirpich::render {

namespace {

// One row of the preview: the kinds from `first` up to `last`, exclusive, laid left to right from
// the x that centers them.
void addRow(Sprites& out, std::string_view keyStem, std::size_t first, std::size_t last, int y,
            const TileAtlas& atlas, std::uint8_t ramp) {
    int width = 0;
    for (std::size_t kind = first; kind < last; ++kind) {
        width += pieceShapeExtent(static_cast<kirpich::PieceKind>(kind)).width;
    }
    width += static_cast<int>(last - first - 1) * kPiecePreviewGap;

    int x = (kScreenWidthPx - width) / 2;
    for (std::size_t kind = first; kind < last; ++kind) {
        const auto    piece = static_cast<kirpich::PieceKind>(kind);
        const Sprites shape = PieceShape(piece, x, y, kPiecePreviewZ,
                                         std::string(keyStem) + "-" + std::to_string(kind), atlas,
                                         ramp);
        out.insert(out.end(), shape.begin(), shape.end());
        x += pieceShapeExtent(piece).width + kPiecePreviewGap;
    }
}

}  // namespace

Sprites PiecePreview(std::string_view keyStem, const TileAtlas& atlas, std::uint8_t ramp) {
    Sprites out;
    addRow(out, keyStem, 0, kPiecePreviewFirstRowCount, kPiecePreviewFirstRowY, atlas, ramp);
    addRow(out, keyStem, kPiecePreviewFirstRowCount, kirpich::kPieceKindCount,
           kPiecePreviewSecondRowY, atlas, ramp);
    return out;
}

}  // namespace kirpich::render
