#include "systems/sprite_renderer.h"

#include <cstddef>
#include <cstdint>

#include "data/new_pieces.h"
#include "data/sprites.h"
#include "state/new_mode_state.h"

namespace kirpich::systems {

namespace {

// One axis of the composed position (tetris.asm:6774-6823). `base` is the descriptor's coordinate on
// this axis, `offset` the sprite's render offset, `part` the part's offset within the sprite, and
// `flip` this axis's flip flag.
//
// Unflipped (:6783-6786) the original adds the descriptor's coordinate to the render offset, then
// adds the part offset with the carry from that first add — one `add` and one `adc`, so the carry
// leaks across. Flipped (:6788-6796) it subtracts instead, on the same borrow chain, and takes a
// further 8 pixels off: a flipped tile is placed by its far edge.
//
// Everything is 8-bit and wraps. That is not incidental — the piece sprites' offsets are negative,
// so the first step overflows on every part, and the leaked carry is part of where a piece appears.
std::uint8_t composeAxis(std::uint8_t base, std::int8_t offset, std::uint8_t part, bool flip) {
    if (!flip) {
        const unsigned sum = static_cast<unsigned>(static_cast<std::uint8_t>(offset)) + base;
        const unsigned carry = sum > 0xFF ? 1U : 0U;
        return static_cast<std::uint8_t>(static_cast<std::uint8_t>(sum) + part + carry);
    }

    const int first = static_cast<int>(base) - static_cast<int>(static_cast<std::uint8_t>(offset));
    const unsigned borrow1 = first < 0 ? 1U : 0U;
    const int second = static_cast<int>(static_cast<std::uint8_t>(first)) - static_cast<int>(part) -
                       static_cast<int>(borrow1);
    const unsigned borrow2 = second < 0 ? 1U : 0U;
    return static_cast<std::uint8_t>(static_cast<int>(static_cast<std::uint8_t>(second)) - 8 -
                                     static_cast<int>(borrow2));
}

// Compose one New-mode shape's cells into consecutive entries, returning how many were written.
//
// A shape is placed a whole tile at a time from the descriptor's position — the same arithmetic the
// piece system's cell query uses, so what collision reads and what the screen shows cannot drift
// apart. There is no render offset and no leaked carry, because there is no sprite record to carry
// them: those are properties of how the cartridge stored its pieces, not of where a piece belongs.
//
// The hidden substitution is the renderer's own, unchanged: y goes off-screen, x stays real, so a
// hidden shape keeps the columns it occupies.
std::size_t renderNewPieceCells(GameContext& game, std::size_t slotIndex, std::size_t oamStart,
                                std::size_t window) {
    EngineState&       engine = game.engine;
    const SpriteSlot&  slot   = game.spriteRenderer.slots[slotIndex];
    const auto         raw    = static_cast<std::uint8_t>(slot.spriteId);
    const NewPieceShape& shape = newPieceShape(raw);
    const std::uint8_t   tile  = newPieceTile(raw);

    // Bounded by the descriptor's own window as well as by the buffer: a shape with more cells than
    // the window holds is truncated here rather than running on into the next descriptor's entries.
    std::size_t written = 0;
    for (std::size_t i = 0; i < shape.count && i < window; ++i) {
        const std::size_t entry = oamStart + i;
        if (entry >= engine.oam.size()) {
            break;
        }
        const NewPieceOffset offset = shape.cells[i];
        const auto           x      = static_cast<std::uint8_t>(slot.x + offset.dx * 8);
        const auto           y      = slot.hidden
                                          ? kHiddenSpriteY
                                          : static_cast<std::uint8_t>(slot.y + offset.dy * 8);

        engine.oam[entry] = OamEntry{
            .y        = y,
            .x        = x,
            .tile     = tile,
            .behindBg = slot.behindBg,
            .yflip    = slot.yflip,
            .xflip    = slot.xflip,
            .palette1 = slot.palette1,
        };
        // Named the same way a walked part is: the descriptor it came from, the identity it is
        // drawing, and which cell of that identity this is. The cell index stands in for the part
        // index, so names stay unique within the frame and change when the shape does.
        game.oamSources.entries[entry] = OamSource{
            .drawn  = true,
            .slot   = static_cast<std::uint8_t>(slotIndex),
            .sprite = slot.spriteId,
            .part   = static_cast<std::uint8_t>(i),
        };
        ++written;
    }
    return written;
}

// Draw one piece descriptor into its window and blank whatever it did not fill.
//
// In a Classic round the window is exactly the four entries a cartridge piece draws, so the blanking
// loop writes nothing and the buffer holds only what the walk put there.
void renderPieceSlot(GameContext& game, std::size_t slotIndex, std::size_t oamStart,
                     std::size_t window) {
    const SpriteSlot& slot = game.spriteRenderer.slots[slotIndex];

    std::size_t drawn = 0;
    if (isNewPiece(static_cast<std::uint8_t>(slot.spriteId))) {
        drawn = renderNewPieceCells(game, slotIndex, oamStart, window);
    } else {
        renderSpriteRange(game, slotIndex, 1, oamStart);
        drawn = getSprite(slot.spriteId).parts.size();
    }

    // A blanked entry is not merely parked off-screen: its source record is cleared too, so the
    // bridge leaves it out of the frame entirely and the ghost does not walk it.
    for (std::size_t i = drawn; i < window; ++i) {
        const std::size_t entry = oamStart + i;
        if (entry >= game.engine.oam.size()) {
            break;
        }
        game.engine.oam[entry]         = OamEntry{};
        game.oamSources.entries[entry] = OamSource{};
    }
}

}  // namespace

SpritePosition spritePartPosition(const SpriteSlot& slot, const Sprite& sprite,
                                  const SpritePart& part) {
    const std::uint8_t x = composeAxis(slot.x, sprite.offset_x, part.x, slot.xflip);

    // The hidden substitution replaces the y the position law produces; it does not replace the law
    // (tetris.asm:6829-6833). The x is the real one either way.
    const std::uint8_t y =
        slot.hidden ? kHiddenSpriteY : composeAxis(slot.y, sprite.offset_y, part.y, slot.yflip);

    return SpritePosition{.y = y, .x = x};
}

void renderSpriteRange(GameContext& game, std::size_t firstSlot, std::size_t count,
                       std::size_t oamStart) {
    EngineState& engine = game.engine;
    const SpriteRendererState& renderer = game.spriteRenderer;
    std::size_t oamIndex = oamStart;

    for (std::size_t i = 0; i < count && firstSlot + i < renderer.slots.size(); ++i) {
        const SpriteSlot& slot = renderer.slots[firstSlot + i];
        const Sprite& sprite = getSprite(slot.spriteId);

        for (std::size_t p = 0; p < sprite.parts.size(); ++p) {
            if (oamIndex >= engine.oam.size()) {
                // The original would keep writing past the buffer into whatever follows it. Nothing
                // shipped goes past: the widest scene is the ending's ten performers, whose four
                // parts each fill the forty entries exactly, to the last one. The walk stops at the
                // end rather than reproducing a corruption nothing reaches.
                return;
            }

            const SpritePart& part = sprite.parts[p];
            const SpritePosition pos = spritePartPosition(slot, sprite, part);

            engine.oam[oamIndex] = OamEntry{
                .y    = pos.y,
                .x    = pos.x,
                .tile = part.tile,
                // The three attribute sources are OR-merged (tetris.asm:6841-6850), which is why the
                // part's flip can only ever set: a part inside an already-flipped descriptor stays
                // flipped rather than flipping back.
                .behindBg = slot.behindBg,
                .yflip    = slot.yflip,
                .xflip    = slot.xflip || part.xflip,
                .palette1 = slot.palette1,
            };

            // Record what this entry now holds, so the bridge can name the object rather than the
            // place. Sprite and part are what make the name survive the object moving and change the
            // moment the descriptor draws something else.
            game.oamSources.entries[oamIndex] = OamSource{
                .drawn  = true,
                .slot   = static_cast<std::uint8_t>(firstSlot + i),
                .sprite = slot.spriteId,
                .part   = static_cast<std::uint8_t>(p),
            };

            ++oamIndex;
        }
    }
}

void renderSprites(GameContext& game, std::size_t count) {
    renderSpriteRange(game, 0, count, kSceneOamStart);
}

void renderCursors(GameContext& game) {
    renderSprites(game, kCursorSpriteCount);
}

void renderActivePieceSprite(GameContext& game) {
    const bool newRound = game.newMode.roundPieceType == PieceType::NEW;
    renderPieceSlot(game, kActivePieceSlot, kActivePieceOamStart,
                    newRound ? kNewModePieceOamSlots : kPieceOamSlots);
}

void renderPreviewPieceSprite(GameContext& game) {
    const bool newRound = game.newMode.roundPieceType == PieceType::NEW;
    renderPieceSlot(game, kPreviewPieceSlot,
                    newRound ? kNewModePreviewPieceOamStart : kPreviewPieceOamStart,
                    newRound ? kNewModePieceOamSlots : kPieceOamSlots);
}

}  // namespace kirpich::systems
