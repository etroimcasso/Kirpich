#pragma once

// Where the Palette settings screen puts things, in viewport pixels.
//
// The heading, the palette row and the cursor are derived from the settings screens' shared cell grid
// (systems/settings_screen.h, through render/option_row.h), so they sit where the settings screen puts
// its own. The swatch and the piece preview are this screen's own, and their numbers are here. Shared
// by the components that draw them and by the tests that read them back.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/background.h"  // kVisibleCols, kVisibleRows
#include "render/option_row.h"
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

// The piece preview: the seven shapes in two rows, each row centered on the shapes' own widths with
// a cell between neighbors. The rows are placed by their tops; every shape is at most two cells tall,
// so the second row's top leaves a cell clear under the first.
inline constexpr int         kPreviewFirstRowY  = 80;
inline constexpr int         kPreviewSecondRowY = 104;
inline constexpr int         kPreviewGap        = kOptionCell;
inline constexpr int         kPreviewShapeSpan  = 2 * kOptionCell;  // the tallest spawn shape
inline constexpr std::size_t kPreviewFirstRowCount = 4;  // L J I O, then S Z T

static_assert(kPreviewFirstRowY >= kSwatchY + kSwatchSquare,
              "the piece preview runs into the swatch above it");
static_assert(kPreviewSecondRowY >= kPreviewFirstRowY + kPreviewShapeSpan,
              "the preview's two rows overlap");
static_assert(kPreviewSecondRowY + kPreviewShapeSpan <= kScreenHeight,
              "the preview runs off the bottom of the screen");

// The content draws over the backdrop; within it the pieces sit under the text.
inline constexpr std::int32_t kContentZ = 10;
inline constexpr std::int32_t kPieceZ   = 10;

}  // namespace kirpich::render::palette_settings
