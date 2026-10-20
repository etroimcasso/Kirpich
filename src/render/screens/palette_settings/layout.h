#pragma once

// Where the Palette settings screen puts things, in viewport pixels.
//
// The heading, the palette row and the cursor are derived from the settings screens' shared cell grid
// (systems/settings_screen.h, through render/components/option_row.h), so they sit where the settings screen puts
// its own. The swatch is this screen's own, and its numbers are here. Shared by the components that
// draw it and by the tests that read them back.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/background.h"  // kVisibleCols, kVisibleRows
#include "render/components/option_row.h"
#include "render/components/piece_preview.h"  // kPiecePreviewFirstRowY
#include "systems/settings_screen.h"

namespace kirpich::render::palette_settings {

inline constexpr std::string_view kHeading = "palette";

inline constexpr int kHeadingX = optionHeadingX(kHeading.size());
inline constexpr int kHeadingY = kOptionHeadingY;
inline constexpr int kPitch    = kOptionCell;
inline constexpr int kCursorX  = kOptionCursorX;

// The palette row: the first line a settings page lays a row on.
inline constexpr std::size_t kPaletteLine = systems::kSettingsFirstRow;
inline constexpr int         kCursorY     = optionPixels(kPaletteLine);

inline constexpr int kScreenWidth  = optionPixels(kVisibleCols);
inline constexpr int kScreenHeight = optionPixels(kVisibleRows);

// The swatch: four squares a cell across, abutting, on the line under the palette row and centered.
inline constexpr int kSwatchSquare  = kOptionCell;
inline constexpr int kSwatchSquares = 4;
inline constexpr int kSwatchWidth   = kSwatchSquare * kSwatchSquares;
inline constexpr int kSwatchX       = (kScreenWidth - kSwatchWidth) / 2;
inline constexpr int kSwatchY       = optionPixels(kPaletteLine + 1);

// The piece preview stands at its own fixed place (render/components/piece_preview.h), under the
// swatch.
static_assert(kPiecePreviewFirstRowY >= kSwatchY + kSwatchSquare,
              "the piece preview runs into the swatch above it");

// The content draws over the backdrop.
inline constexpr std::int32_t kContentZ = 10;

}  // namespace kirpich::render::palette_settings
