#include "render/screens/palette_editor/palette_preview.h"

#include <cstddef>

#include "render/components/glyphs.h"
#include "render/components/piece_preview.h"
#include "render/screens/palette_editor/layout.h"

namespace kirpich::render {

using namespace palette_editor;

namespace {

[[nodiscard]] int wordX(std::size_t i) noexcept {
    return previewWordX(kPreviewWords[i].size());
}

}  // namespace

Sprites PalettePreview(const TileAtlas& atlas, std::uint8_t ramp) {
    Sprites    out;
    const auto add = [&out](const Sprites& part) { out.insert(out.end(), part.begin(), part.end()); };
    add(SelectedGlyphs(kPreviewWords[0], wordX(0), previewWordY(0), kGlyph, atlas, ramp));
    add(Glyphs(kPreviewWords[1], wordX(1), previewWordY(1), kGlyph, atlas, ramp));
    add(DimGlyphs(kPreviewWords[2], wordX(2), previewWordY(2), kGlyph, atlas, ramp));
    add(PiecePreview("preview-piece", atlas, ramp));
    return out;
}

}  // namespace kirpich::render
