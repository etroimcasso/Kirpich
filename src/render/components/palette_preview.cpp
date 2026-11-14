#include "render/components/palette_preview.h"

#include <cstddef>

#include "render/components/glyphs.h"
#include "render/components/piece_preview.h"

namespace kirpich::render {

namespace {

constexpr int kGlyph = kPalettePreviewGlyph;

[[nodiscard]] int wordX(std::size_t i) noexcept {
    return palettePreviewWordX(kPalettePreviewWords[i].size());
}

[[nodiscard]] int wordY(std::size_t i) noexcept {
    return palettePreviewWordY(i);
}

}  // namespace

Sprites PalettePreview(const TileAtlas& atlas, std::uint8_t ramp) {
    Sprites    out;
    const auto add = [&out](const Sprites& part) { out.insert(out.end(), part.begin(), part.end()); };
    add(SelectedGlyphs(kPalettePreviewWords[0], wordX(0), wordY(0), kGlyph, atlas, ramp));
    add(Glyphs(kPalettePreviewWords[1], wordX(1), wordY(1), kGlyph, atlas, ramp));
    add(DimGlyphs(kPalettePreviewWords[2], wordX(2), wordY(2), kGlyph, atlas, ramp));
    add(PiecePreview("preview-piece", atlas, ramp));
    return out;
}

}  // namespace kirpich::render
