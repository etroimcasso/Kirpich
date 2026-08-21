#pragma once

// The sprite bridge: the object buffer, drawn.
//
// The object buffer (EngineState::oam) is the port's model of the hardware's object list, and the
// sprite renderer fills it from the game's own descriptors. So the objects on screen are a pure
// function of simulation state in the same way the background is, and this is that function: read
// each of the forty entries, resolve its tile against the live art regime, and hand the result to
// the engine as one sprite layer above the background.
//
// An object is named by what it IS, not by where in the buffer it landed - the renderer records
// which descriptor and which part of that descriptor's sprite each entry holds, and that is the
// name. The engine matches an object to its own previous frame by name and eases it between the two
// positions, so naming it after the entry would be a bug with a picture: entries shift whenever a
// descriptor's art changes width, and two unrelated objects sharing a name read as one that
// travelled across the screen.
//
// Entries that draw nothing are not submitted at all. The buffer's coordinates are offset from the
// screen's, so an untouched entry sits above and left of the first pixel and a hidden one below the
// last; leaving them out means an object that comes back appears where it is, rather than sliding
// in from wherever it was parked.
//
// What this does NOT do, all three named in docs/contracts/sprite-renderer.md as visible
// differences: object-over-background priority (the attribute is carried and ignored - no screen the
// port draws sets it), the hardware's per-scanline object limit (which made crowded rows flicker),
// and its left-to-right priority rule (here a lower buffer entry simply draws on top).

#include <cstddef>
#include <cstdint>
#include <vector>

#include <retropp/draw_state.h>  // DrawLayer, Sprite
#include <retropp/viewport.h>    // ViewportResolution

#include "render/tile_atlas.h"
#include "state/display_state.h"
#include "state/engine_state.h"
#include "systems/oam_source.h"

namespace kirpich::render {

// The layer's identity and depth. One sprite layer, drawn over the background.
inline constexpr const char* kSpriteLayerKey = "sprites";
inline constexpr std::int32_t kSpriteLayerZ  = 1;

// The hardware offsets an object's stored coordinates carry: a stored y of 16 is the screen's top
// row, and a stored x of 8 its first column.
inline constexpr int kSpriteScreenOffsetY = 16;
inline constexpr int kSpriteScreenOffsetX = 8;

// The screen an object is placed on, and the size of one object. Every object is one tile: the
// game leaves the hardware in its 8x8 object mode and composes bigger shapes out of several.
inline constexpr int kScreenWidthPx  = 160;
inline constexpr int kScreenHeightPx = 144;
inline constexpr int kSpriteSizePx   = 8;

// Compose the object buffer into placed sprites, skipping the entries that draw nothing.
//
// `sources` is the renderer's record of what drew each entry, which is where the names come from.
// `tick` must INCREMENT once per simulation tick and is otherwise arbitrary - it goes into every
// name, and a name the renderer has not just seen is what stops it easing an object from its
// previous position into its new one. Passing a constant makes objects glide between their steps;
// passing a value that repeats within a few ticks makes them do it intermittently, which is worse.
//
// Writes into `sprites` rather than returning a fresh vector so a caller can keep one buffer for the
// whole run - the frame is rebuilt every time.
//
// `ramp` is the shade ramp the player has chosen (src/render/palettes.h). Objects take it with its
// last entry made see-through, which is the hardware's own rule.
void composeSprites(const EngineState& engine, const OamSourceTable& sources, TileSheet sheet,
                    std::uint16_t tick, const TileAtlas& atlas,
                    std::vector<retropp::Sprite>& sprites, std::uint8_t ramp = kDefaultShadeRamp);

// Wrap composed sprites as the frame's object layer.
//
// The layer BORROWS the sprites - the engine's sprite content holds a span, valid only for the
// renderFrame call that consumes it - so the vector must outlive that call. Keep it alive across the
// frame; do not hand this a temporary.
[[nodiscard]] retropp::DrawLayer spriteLayer(
    const std::vector<retropp::Sprite>& sprites,
    retropp::ViewportResolution viewport = retropp::ViewportResolution::GameBoy);

}  // namespace kirpich::render
