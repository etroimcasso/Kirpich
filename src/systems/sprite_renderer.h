#pragma once

// The sprite renderer: sprite descriptors compiled into the OAM buffer.
//
// The game keeps sixteen sprite descriptors (SpriteRendererState) — a position, a composed-sprite
// id, and a few attribute flags each — and one flat buffer of forty hardware object entries
// (EngineState::oam). This is the routine that turns the first into the second. Every menu cursor,
// the falling piece, the next-piece preview and the ending's dancers reach the screen through it.
//
// The composition it consumes is already resolved: a composed Sprite (src/data/sprites.h) carries
// its parts as plain (y, x, xflip, tile) rows, so nothing here walks the original's escape stream or
// its pixel grids. What remains is the walk over descriptors — where each part lands, which
// attributes it carries, and where in the buffer it goes.
//
// Three things about that walk are easy to get wrong and are load-bearing:
//
//   * The OAM cursor runs on. It advances one entry per drawn part and is NOT reset between
//     descriptors, so rendering two descriptors from entry 0 fills as many consecutive entries as
//     their parts total. That is what lets the cursor pair occupy entries 0 and 1 and the dancers
//     fill the buffer from the front.
//   * A hidden descriptor still draws. It is not skipped: every part is written with its real x,
//     tile and attributes, and only the y is replaced by an off-screen value. The piece system
//     depends on this — a hidden piece still collides on the columns it really occupies.
//   * The attribute merge is an OR. A part's x-flip can set the flip bit but can never clear one the
//     descriptor already carries.
//
// The exact laws, with source line anchors, are in docs/contracts/sprite-renderer.md.

#include <cstddef>
#include <cstdint>

#include "data/sprites.h"                 // Sprite, SpritePart
#include "state/engine_state.h"           // EngineState, OamEntry
#include "state/sprite_renderer_state.h"  // SpriteRendererState, SpriteSlot
#include "systems/game_context.h"

namespace kirpich::systems {

// The y value a hidden descriptor's parts are written with. Past the bottom of the screen, so the
// object is invisible while its entry still exists and still carries its real column.
inline constexpr std::uint8_t kHiddenSpriteY = 0xFF;

// Where each entry point starts writing. The cursor pair and the scene forms fill from the front;
// the two piece descriptors have fixed homes the rest of the buffer is laid out around.
inline constexpr std::size_t kSceneOamStart        = 0;
inline constexpr std::size_t kActivePieceOamStart  = 4;
inline constexpr std::size_t kPreviewPieceOamStart = 8;

// How many entries each piece descriptor owns.
//
// A cartridge piece is four tiles, so four entries hold one. A New round can put a five-cell shape
// in either slot, so both windows are five entries there and the preview's home moves up by one to
// make room. The width is a property of the ROUND, not of the piece in the slot: a four-tile
// cartridge piece drawn during a New round still owns five entries and leaves the last one blank,
// because the piece that follows it may need all five.
//
// Entries past the preview window are free during a round: the round init clears the buffer and the
// two piece descriptors are the only things that draw while a round is being played.
inline constexpr std::size_t kPieceOamSlots               = 4;
inline constexpr std::size_t kNewModePieceOamSlots        = 5;
inline constexpr std::size_t kNewModePreviewPieceOamStart = kActivePieceOamStart + kNewModePieceOamSlots;

// How many descriptors the cursor form draws.
inline constexpr std::size_t kCursorSpriteCount = 2;

// One part's composed position in OAM coordinates: y counts down from 16 pixels above the screen, x
// right from 8 pixels left of it, both wrapping at 8 bits.
struct SpritePosition {
    std::uint8_t y = 0;
    std::uint8_t x = 0;

    friend constexpr bool operator==(const SpritePosition&, const SpritePosition&) = default;
};

// Where one part of a descriptor's sprite lands.
//
// Three terms compose: the descriptor's position, the sprite's own two render offsets, and the
// part's offset within the sprite. The original adds them as a pair of 8-bit adds that share their
// carry, so the carry out of the first leaks into the second — and because the piece sprites'
// render offsets are negative, that carry fires on every part of every piece. Flipping is
// subtractive rather than additive and carries an extra 8-pixel correction, on the same borrow
// chain.
//
// A hidden descriptor yields kHiddenSpriteY for y and its real x, which is the substitution the
// original makes when it writes the entry rather than a different position law.
[[nodiscard]] SpritePosition spritePartPosition(const SpriteSlot& slot, const Sprite& sprite,
                                                const SpritePart& part);

// Draw `count` descriptors starting at `firstSlot` into the buffer starting at entry `oamStart`.
// Each descriptor contributes one entry per part of its composed sprite, consecutively. Entries the
// walk does not reach are left alone — the buffer is persistent, and a caller that wants it blank
// clears it first.
//
// Also records what each written entry now holds (systems/oam_source.h): the drawing is only half of
// putting an object on screen, and the other half is being able to say which object it is.
void renderSpriteRange(GameContext& game, std::size_t firstSlot, std::size_t count,
                       std::size_t oamStart);

// The four entry points the handlers call, each a fixed choice of descriptor range and destination.

// `count` descriptors from the first, into the buffer from the front. The scene form.
void renderSprites(GameContext& game, std::size_t count);

// The first two descriptors, into the buffer from the front. Every menu screen's cursors.
void renderCursors(GameContext& game);

// The active piece's descriptor, into its fixed home at entry 4.
//
// A New-mode shape carries no composed sprite, so these two compose its entries from its cell list
// instead of walking one — the only place in the renderer that knows New mode exists. Whichever way
// the entries are produced, the descriptor's own window is filled to the end: entries the piece does
// not need are blanked, so a three-cell shape following a five-cell one leaves no block behind.
void renderActivePieceSprite(GameContext& game);

// The preview piece's descriptor, into its fixed home — entry 8, or entry 9 during a New round.
void renderPreviewPieceSprite(GameContext& game);

}  // namespace kirpich::systems
