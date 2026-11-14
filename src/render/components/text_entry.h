#pragma once

// A text input screen, as the layer it takes the screen with: a title, and a run of characters with
// one place picked out for the player to change.
//
// The title stands centered on the heading's line. The text stands centered as a run on its own line,
// one glyph a place, every place through the selected font palette (SelectedGlyphs); the glyph at
// `place` blinks - it is drawn only while `blinkOn` - and an up arrow stands over it and a down arrow
// under it, named `<key>-up` and `<key>-down`. A space draws nothing and still takes its place.
//
// It draws what it is handed and knows nothing of what is being named or how a character is turned:
// the words, the characters and the place are the caller's. The rotation editor raises it to name a
// saved rotation (render/screens/rotation_editor/screen.h).

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <retropp/draw_state.h>  // DrawLayer

#include "render/sprites.h"  // kScreenWidthPx
#include "render/tile_atlas.h"

namespace kirpich::render {

// The title's line and the text's line, a glyph's side, and how far the arrows stand above and below
// the place.
inline constexpr int kTextEntryTitleY    = 16;
inline constexpr int kTextEntryTextY     = 64;
inline constexpr int kTextEntryGlyph     = 8;
inline constexpr int kTextEntryArrowRise = 8;

// The layer draws over its screen's backdrop.
inline constexpr std::int32_t kTextEntryZ = 10;

// Where a run `length` glyphs long starts so it is centered across the screen.
[[nodiscard]] constexpr int textEntryTextX(std::size_t length) noexcept {
    return (kScreenWidthPx - static_cast<int>(length) * kTextEntryGlyph) / 2;
}

// The sprite layer named `key`, at kTextEntryZ, through the object palettes of `ramp`: `title`, then
// `text` with the glyph at `place` drawn while `blinkOn` and the two arrows at it.
[[nodiscard]] retropp::DrawLayer TextEntry(std::string_view key, std::string_view title,
                                           std::string_view text, std::size_t place, bool blinkOn,
                                           const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
