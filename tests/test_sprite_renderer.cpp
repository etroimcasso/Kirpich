// Sprite renderer — behavioral tests against docs/contracts/sprite-renderer.md.
//
// Device-free: compiling descriptors into the object buffer is pure logic over the game-state
// aggregate. Every asserted value is traced to the tetris.asm lines named in the contract
// (_RenderSprites :6687-6856 and its four entry points :6218-6252). No ROM read, no renderer, no
// virtual machine.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include <kirpich/sprite_id.h>

#include "data/sprites.h"
#include "state/engine_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/sprite_renderer.h"

using kirpich::getSprite;
using kirpich::OamEntry;
using kirpich::Sprite;
using kirpich::SpriteId;
using kirpich::SpritePart;
using kirpich::SpriteSlot;
using kirpich::systems::GameContext;
using kirpich::systems::SpritePosition;

namespace {

// A descriptor at a known place, drawing a known sprite.
SpriteSlot descriptor(SpriteId id, std::uint8_t y, std::uint8_t x) {
    SpriteSlot slot;
    slot.spriteId = id;
    slot.y = y;
    slot.x = x;
    return slot;
}

// The position law, restated from the routine rather than from the implementation: the descriptor's
// coordinate plus the sprite's render offset, then the part's own offset with the carry out of that
// first add (tetris.asm:6783-6786) — or, flipped, the same chain subtracting, with a further eight
// pixels off (:6788-6796).
std::uint8_t axisByHand(std::uint8_t base, std::int8_t offset, std::uint8_t part, bool flip) {
    const auto off = static_cast<std::uint8_t>(offset);
    if (!flip) {
        const unsigned first = static_cast<unsigned>(off) + base;
        return static_cast<std::uint8_t>(static_cast<std::uint8_t>(first) + part +
                                         (first > 0xFF ? 1U : 0U));
    }
    const int first = static_cast<int>(base) - static_cast<int>(off);
    const int second = static_cast<int>(static_cast<std::uint8_t>(first)) - static_cast<int>(part) -
                       (first < 0 ? 1 : 0);
    return static_cast<std::uint8_t>(static_cast<int>(static_cast<std::uint8_t>(second)) - 8 -
                                     (second < 0 ? 1 : 0));
}

}  // namespace

// ── Test 1: EntryPointWindows ─────────────────────────────────────────────────────────────────────
// The four entry points (tetris.asm:6218-6252): which descriptors each draws, and where in the buffer
// it starts. The buffer cursor runs on across parts AND across descriptors (:6825-6854) — it is never
// reset per descriptor — and entries the walk does not reach keep whatever they held.
TEST(SpriteRenderer, EntryPointWindows) {
    // Cursors: two descriptors from the first, into the buffer from the front (:6218-6227).
    {
        GameContext game;
        game.spriteRenderer.slots[0] = descriptor(SpriteId::DIGIT_0, 0x40, 0x30);
        game.spriteRenderer.slots[1] = descriptor(SpriteId::DIGIT_1, 0x50, 0x38);
        game.engine.oam[20] = OamEntry{.y = 0x77};  // beyond the walk

        kirpich::systems::renderCursors(game);

        const std::size_t first = getSprite(SpriteId::DIGIT_0).parts.size();
        const std::size_t total = first + getSprite(SpriteId::DIGIT_1).parts.size();
        EXPECT_EQ(game.oamSources.entries[0].slot, 0u);
        EXPECT_EQ(game.oamSources.entries[first].slot, 1u)
            << "the second descriptor continues where the first stopped";
        for (std::size_t i = 0; i < total; ++i) {
            EXPECT_TRUE(game.oamSources.entries[i].drawn) << "entry " << i;
        }
        EXPECT_FALSE(game.oamSources.entries[total].drawn) << "the walk stops after the last part";
        EXPECT_EQ(game.engine.oam[20].y, 0x77) << "entries past the walk are left alone";
    }

    // The active piece goes to entry 4, the preview to entry 8, one descriptor each (:6231-6252).
    {
        GameContext game;
        game.spriteRenderer.slots[kirpich::kActivePieceSlot] =
            descriptor(SpriteId::L_0, 0x18, 0x3F);
        game.spriteRenderer.slots[kirpich::kPreviewPieceSlot] =
            descriptor(SpriteId::J_0, 0x80, 0x8F);

        kirpich::systems::renderActivePieceSprite(game);
        kirpich::systems::renderPreviewPieceSprite(game);

        EXPECT_TRUE(game.oamSources.entries[4].drawn);
        EXPECT_EQ(game.oamSources.entries[4].sprite, SpriteId::L_0);
        EXPECT_TRUE(game.oamSources.entries[8].drawn);
        EXPECT_EQ(game.oamSources.entries[8].sprite, SpriteId::J_0);
        EXPECT_FALSE(game.oamSources.entries[0].drawn) << "the piece forms do not touch the front";
        // A piece composes to four parts, so each fills exactly its own four entries.
        EXPECT_FALSE(game.oamSources.entries[3].drawn);
        EXPECT_TRUE(game.oamSources.entries[7].drawn);
        EXPECT_FALSE(game.oamSources.entries[12].drawn);
    }

    // The scene form draws as many descriptors as it is given, from the front. The ending's ten
    // performers are the widest scene shipped and fill the buffer exactly (:4816-4817).
    {
        GameContext game;
        for (std::size_t i = 0; i < 10; ++i) {
            game.spriteRenderer.slots[i] = descriptor(SpriteId::VIOLINIST_1, 0x40, 0x20);
        }
        kirpich::systems::renderSprites(game, 10);

        EXPECT_TRUE(game.oamSources.entries[39].drawn) << "ten performers reach the last entry";
        EXPECT_EQ(game.oamSources.entries[39].slot, 9u);
    }
}

// ── Test 2: HiddenAndVisibleDescriptors ───────────────────────────────────────────────────────────
// The status byte's two shipped cases (tetris.asm:6693-6697, :6829-6833). A hidden descriptor is NOT
// skipped: every part is still written, with its real x, tile and attributes, and only the y replaced
// by a value past the bottom of the screen. The piece system depends on that — a hidden piece still
// collides on the columns it really occupies.
TEST(SpriteRenderer, HiddenAndVisibleDescriptors) {
    GameContext visible;
    visible.spriteRenderer.slots[0] = descriptor(SpriteId::L_0, 0x40, 0x30);
    kirpich::systems::renderSprites(visible, 1);

    GameContext hidden;
    hidden.spriteRenderer.slots[0] = descriptor(SpriteId::L_0, 0x40, 0x30);
    hidden.spriteRenderer.slots[0].hidden = true;
    kirpich::systems::renderSprites(hidden, 1);

    const std::size_t parts = getSprite(SpriteId::L_0).parts.size();
    for (std::size_t i = 0; i < parts; ++i) {
        EXPECT_EQ(hidden.engine.oam[i].y, kirpich::systems::kHiddenSpriteY) << "entry " << i;
        EXPECT_EQ(hidden.engine.oam[i].x, visible.engine.oam[i].x) << "the real column is kept";
        EXPECT_EQ(hidden.engine.oam[i].tile, visible.engine.oam[i].tile);
        EXPECT_TRUE(hidden.oamSources.entries[i].drawn) << "a hidden descriptor still occupies it";
    }
    EXPECT_NE(visible.engine.oam[0].y, kirpich::systems::kHiddenSpriteY);
}

// ── Test 3: PositionLawFullCorpus ─────────────────────────────────────────────────────────────────
// The composed position for every one of the 94 sprites (tetris.asm:6774-6823), against the law
// re-derived by hand above rather than read back from the implementation. The unflipped chain leaks
// the carry out of its first add into the second — which fires on every part of every piece, whose
// render offsets are negative — and each flip axis subtracts instead, on the same borrow chain, with
// a further eight pixels off.
TEST(SpriteRenderer, PositionLawFullCorpus) {
    constexpr std::uint8_t kY = 0x4C;  // deliberately not a multiple of 8, so a carry can show
    constexpr std::uint8_t kX = 0x3F;

    for (std::size_t id = 0; id < 94; ++id) {
        const auto spriteId = static_cast<SpriteId>(id);
        const Sprite& sprite = getSprite(spriteId);

        for (const bool yflip : {false, true}) {
            for (const bool xflip : {false, true}) {
                SpriteSlot slot = descriptor(spriteId, kY, kX);
                slot.yflip = yflip;
                slot.xflip = xflip;

                for (std::size_t p = 0; p < sprite.parts.size(); ++p) {
                    const SpritePart& part = sprite.parts[p];
                    const SpritePosition pos =
                        kirpich::systems::spritePartPosition(slot, sprite, part);

                    EXPECT_EQ(pos.y, axisByHand(kY, sprite.offset_y, part.y, yflip))
                        << "sprite " << id << " part " << p << " yflip " << yflip;
                    EXPECT_EQ(pos.x, axisByHand(kX, sprite.offset_x, part.x, xflip))
                        << "sprite " << id << " part " << p << " xflip " << xflip;
                }
            }
        }
    }

    // The carry is not decorative: the spawn position of a piece depends on it. L_0's render offsets
    // are -17 and -16, so the first add overflows on every part.
    const Sprite& l0 = getSprite(SpriteId::L_0);
    EXPECT_LT(l0.offset_y, 0);
    EXPECT_LT(l0.offset_x, 0);
    const SpritePosition spawn =
        kirpich::systems::spritePartPosition(descriptor(SpriteId::L_0, 0x18, 0x3F), l0, l0.parts[0]);
    EXPECT_EQ(spawn.y, 0x18 + static_cast<std::uint8_t>(l0.offset_y) + l0.parts[0].y + 1 - 0x100)
        << "the carry out of the first add lands in the second";
}

// ── Test 4: AttributeMergeIsAnOr ──────────────────────────────────────────────────────────────────
// The final attribute is the descriptor's three attribute sources merged with the part's own flip
// (tetris.asm:6841-6850). The merge is an OR, so a part's flip can SET the horizontal bit but can
// never clear one the descriptor already carries — a flipped part inside a flipped descriptor stays
// flipped.
TEST(SpriteRenderer, AttributeMergeIsAnOr) {
    // A sprite with at least one flipped part, so the escape's effect is observable.
    const SpriteId flipped = SpriteId::ROCKET_S;
    const Sprite& sprite = getSprite(flipped);
    std::size_t flippedPart = sprite.parts.size();
    std::size_t plainPart = sprite.parts.size();
    for (std::size_t i = 0; i < sprite.parts.size(); ++i) {
        if (sprite.parts[i].xflip && flippedPart == sprite.parts.size()) flippedPart = i;
        if (!sprite.parts[i].xflip && plainPart == sprite.parts.size()) plainPart = i;
    }
    ASSERT_LT(flippedPart, sprite.parts.size()) << "this sprite must carry a flipped part";
    ASSERT_LT(plainPart, sprite.parts.size()) << "and a plain one";

    // An unflipped descriptor: the part decides.
    {
        GameContext game;
        game.spriteRenderer.slots[0] = descriptor(flipped, 0x40, 0x40);
        kirpich::systems::renderSprites(game, 1);
        EXPECT_TRUE(game.engine.oam[flippedPart].xflip);
        EXPECT_FALSE(game.engine.oam[plainPart].xflip);
    }

    // A flipped descriptor: every part comes out flipped, including the one the stream flips again.
    {
        GameContext game;
        game.spriteRenderer.slots[0] = descriptor(flipped, 0x40, 0x40);
        game.spriteRenderer.slots[0].xflip = true;
        kirpich::systems::renderSprites(game, 1);
        EXPECT_TRUE(game.engine.oam[flippedPart].xflip) << "the escape cannot cancel the descriptor";
        EXPECT_TRUE(game.engine.oam[plainPart].xflip);
    }

    // The other three attribute bits come from the descriptor alone and reach every part.
    {
        GameContext game;
        game.spriteRenderer.slots[0] = descriptor(SpriteId::L_0, 0x40, 0x40);
        game.spriteRenderer.slots[0].behindBg = true;
        game.spriteRenderer.slots[0].yflip = true;
        game.spriteRenderer.slots[0].palette1 = true;
        kirpich::systems::renderSprites(game, 1);
        for (std::size_t i = 0; i < getSprite(SpriteId::L_0).parts.size(); ++i) {
            EXPECT_TRUE(game.engine.oam[i].behindBg) << "entry " << i;
            EXPECT_TRUE(game.engine.oam[i].yflip) << "entry " << i;
            EXPECT_TRUE(game.engine.oam[i].palette1) << "entry " << i;
        }
    }
}

// ── Test 5: RecordedIdentity ──────────────────────────────────────────────────────────────────────
// What the renderer records about each entry it writes (systems/oam_source.h). This is not a hardware
// behaviour — it is how the port tells one frame's objects from the last frame's — but the drawing is
// only half of putting an object on screen, and a wrong record shows up as an object sliding across
// the screen rather than as a wrong pixel.
TEST(SpriteRenderer, RecordedIdentity) {
    GameContext game;
    game.spriteRenderer.slots[0] = descriptor(SpriteId::DIGIT_3, 0x40, 0x30);
    game.spriteRenderer.slots[1] = descriptor(SpriteId::L_0, 0x50, 0x38);
    kirpich::systems::renderCursors(game);

    const std::size_t first = getSprite(SpriteId::DIGIT_3).parts.size();
    EXPECT_EQ(game.oamSources.entries[0].sprite, SpriteId::DIGIT_3);
    EXPECT_EQ(game.oamSources.entries[0].part, 0u);
    EXPECT_EQ(game.oamSources.entries[first].slot, 1u);
    EXPECT_EQ(game.oamSources.entries[first].sprite, SpriteId::L_0);
    EXPECT_EQ(game.oamSources.entries[first].part, 0u);
    EXPECT_EQ(game.oamSources.entries[first + 1].part, 1u) << "parts number within their sprite";

    // A descriptor that starts drawing something else replaces the record, which is what makes the
    // new art a new object rather than the old one having moved.
    game.spriteRenderer.slots[0] = descriptor(SpriteId::DIGIT_4, 0x40, 0x30);
    kirpich::systems::renderCursors(game);
    EXPECT_EQ(game.oamSources.entries[0].sprite, SpriteId::DIGIT_4);

    // Clearing the buffer forgets every record with it: an emptied buffer holds no objects, so there
    // is nothing left for the next screen's objects to be mistaken for.
    kirpich::systems::clearOamObjects(game);
    for (const auto& src : game.oamSources.entries) {
        EXPECT_FALSE(src.drawn);
    }
}
