// Sprite bridge — behavioral tests against docs/contracts/sprite-renderer.md §"the bridge".
//
// Device-free: composing the object buffer into placed sprites needs no renderer, so the uploaded
// handles are left at their default values and only the choices the bridge makes are asserted —
// which entries are submitted, where they land, which art and palette each names, and what each is
// called. The palette contents themselves are derived from the object palettes the original writes
// (tetris.asm:296-300) and the decode's inversion, and are checked here as a relation rather than
// through an upload.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <kirpich/sprite_id.h>

#include "data/sprites.h"
#include "render/background.h"  // kBackgroundLayerZ
#include "render/sprites.h"
#include "render/tile_atlas.h"
#include "state/display_state.h"
#include "state/engine_state.h"
#include "systems/game_context.h"
#include "systems/sprite_renderer.h"

using kirpich::OamEntry;
using kirpich::SpriteId;
using kirpich::SpriteSlot;
using kirpich::TileSheet;
using kirpich::render::composeSprites;
using kirpich::render::kScreenHeightPx;
using kirpich::render::kScreenWidthPx;
using kirpich::render::kSpriteScreenOffsetX;
using kirpich::render::kSpriteScreenOffsetY;
using kirpich::render::spriteLayer;
using kirpich::render::TileAtlas;
using kirpich::systems::GameContext;

namespace {

// Distinct handles, so "which sheet" and "which palette" are answerable without a device. The engine
// hands these out at upload; here they only need to differ from each other.
TileAtlas labelledAtlas() {
    TileAtlas atlas;
    atlas.font = retropp::AtlasId{1};
    atlas.copyrightTitle = retropp::AtlasId{2};
    atlas.gameplay = retropp::AtlasId{3};
    atlas.multiplayerBuran = retropp::AtlasId{4};
    atlas.fontPalette = retropp::PaletteId{10};
    atlas.contentPalette = retropp::PaletteId{11};
    atlas.fontSpritePalette = retropp::PaletteId{12};
    atlas.spritePalette0 = retropp::PaletteId{13};
    atlas.spritePalette1 = retropp::PaletteId{14};
    return atlas;
}

std::vector<retropp::Sprite> compose(const GameContext& game, TileSheet sheet,
                                     const TileAtlas& atlas, std::uint16_t tick = 0) {
    std::vector<retropp::Sprite> out;
    composeSprites(game.engine, game.oamSources, sheet, tick, atlas, out);
    return out;
}

}  // namespace

// ── Test 1: OnlyDrawingEntriesAreSubmitted ────────────────────────────────────────────────────────
// An entry's stored coordinates are offset from the screen's, so an untouched entry sits above and
// left of the first pixel and a hidden one below the last (tetris.asm:6829-6833). Neither draws, and
// neither is submitted — an object that draws nothing has no position for the next frame to be eased
// from.
TEST(SpriteBridge, OnlyDrawingEntriesAreSubmitted) {
    const TileAtlas atlas = labelledAtlas();
    GameContext game;

    // A boot buffer is entirely untouched entries.
    EXPECT_TRUE(compose(game, TileSheet::GAMEPLAY, atlas).empty())
        << "an empty buffer places nothing";

    // One visible entry, one hidden, one parked at the buffer's origin.
    game.engine.oam[0] = OamEntry{.y = 0x40, .x = 0x30, .tile = 0x80};
    game.engine.oam[1] = OamEntry{.y = kirpich::systems::kHiddenSpriteY, .x = 0x30, .tile = 0x80};
    game.engine.oam[2] = OamEntry{.y = 0x00, .x = 0x00, .tile = 0x80};

    const auto placed = compose(game, TileSheet::GAMEPLAY, atlas);
    ASSERT_EQ(placed.size(), 1u) << "only the visible entry is placed";
    EXPECT_EQ(placed[0].y, 0x40 - kSpriteScreenOffsetY);
    EXPECT_EQ(placed[0].x, 0x30 - kSpriteScreenOffsetX);

    // The boundary either way: one pixel of an object on screen is enough, and none is not.
    for (const auto& [y, x, drawn] :
         std::initializer_list<std::tuple<std::uint8_t, std::uint8_t, bool>>{
             {kSpriteScreenOffsetY, kSpriteScreenOffsetX, true},         // the top-left pixel
             {kSpriteScreenOffsetY - 1, kSpriteScreenOffsetX, true},     // one row above, 7 rows show
             {kSpriteScreenOffsetY - 8, kSpriteScreenOffsetX, false},    // fully above
             {kSpriteScreenOffsetY + kScreenHeightPx - 1, kSpriteScreenOffsetX, true},
             {kSpriteScreenOffsetY + kScreenHeightPx, kSpriteScreenOffsetX, false},
             {kSpriteScreenOffsetY, kSpriteScreenOffsetX + kScreenWidthPx, false},
         }) {
        GameContext one;
        one.engine.oam[0] = OamEntry{.y = y, .x = x, .tile = 0x80};
        EXPECT_EQ(compose(one, TileSheet::GAMEPLAY, atlas).size(), drawn ? 1u : 0u)
            << "y " << int{y} << " x " << int{x};
    }
}

// ── Test 2: ArtAndPaletteSelection ────────────────────────────────────────────────────────────────
// A tile index means the same art for an object as it does for the background — objects index the
// same block the loaders write — so the sheet comes from the shipped resolution. The palette does
// not: objects have their own, and which of the two the game keeps (tetris.asm:296-300) is the
// entry's own attribute.
TEST(SpriteBridge, ArtAndPaletteSelection) {
    const TileAtlas atlas = labelledAtlas();

    struct Row {
        std::uint8_t tile;
        TileSheet sheet;
        bool palette1;
        retropp::AtlasId expectedAtlas;
        retropp::PaletteId expectedPalette;
    };
    const Row rows[] = {
        // The font block is the first 39 indices under either regime, and its two colours are the
        // same through both object palettes, so one palette serves whichever is selected.
        {0x00, TileSheet::GAMEPLAY, false, atlas.font, atlas.fontSpritePalette},
        {0x00, TileSheet::GAMEPLAY, true, atlas.font, atlas.fontSpritePalette},
        {0x26, TileSheet::COPYRIGHT_TITLE, false, atlas.font, atlas.fontSpritePalette},
        // Content art takes the object palette the attribute names.
        {0x84, TileSheet::GAMEPLAY, false, atlas.gameplay, atlas.spritePalette0},
        {0x84, TileSheet::GAMEPLAY, true, atlas.gameplay, atlas.spritePalette1},
        {0x28, TileSheet::COPYRIGHT_TITLE, false, atlas.copyrightTitle, atlas.spritePalette0},
    };

    for (const Row& row : rows) {
        GameContext game;
        game.engine.oam[0] =
            OamEntry{.y = 0x40, .x = 0x30, .tile = row.tile, .palette1 = row.palette1};
        const auto placed = compose(game, row.sheet, atlas);
        ASSERT_EQ(placed.size(), 1u);
        EXPECT_EQ(placed[0].atlas, row.expectedAtlas) << "tile " << int{row.tile};
        EXPECT_EQ(placed[0].palette, row.expectedPalette) << "tile " << int{row.tile};
    }

    // The flips ride across untouched.
    GameContext flipped;
    flipped.engine.oam[0] =
        OamEntry{.y = 0x40, .x = 0x30, .tile = 0x84, .yflip = true, .xflip = true};
    const auto placed = compose(flipped, TileSheet::GAMEPLAY, atlas);
    ASSERT_EQ(placed.size(), 1u);
    EXPECT_TRUE(placed[0].flipX);
    EXPECT_TRUE(placed[0].flipY);
}

// ── Test 3: ObjectPalettesAreTheGamesOwn ──────────────────────────────────────────────────────────
// The object palettes are the two the original writes at startup (tetris.asm:296-300) read through
// the decode's inversion, which is what puts the see-through entry at the END of each ramp rather
// than at its start. Getting that backwards draws every object on a solid card.
TEST(SpriteBridge, ObjectPalettesAreTheGamesOwn) {
    using kirpich::render::kShadeDark;
    using kirpich::render::kShadeDarkest;
    using kirpich::render::kShadeLight;
    using kirpich::render::kShadeLightest;
    using kirpich::render::kShadeTransparent;

    // Transparency is an alpha of zero, and no shade shares it.
    EXPECT_EQ(kShadeTransparent.a, 0u);
    for (const auto& shade : {kShadeDarkest, kShadeDark, kShadeLight, kShadeLightest}) {
        EXPECT_EQ(shade.a, 255u) << "a shade is opaque";
    }

    // The plain object ramp is the background's with its last entry made see-through: sample index i
    // is hardware colour 3 - i, and hardware colour 0 is the see-through one.
    const std::array<retropp::Rgba8, 4> expected0{kShadeDarkest, kShadeDark, kShadeLight,
                                                  kShadeTransparent};
    // The variant differs in one place: the colour that is ordinarily second-darkest draws lightest.
    const std::array<retropp::Rgba8, 4> expected1{kShadeDarkest, kShadeLightest, kShadeLight,
                                                  kShadeTransparent};
    EXPECT_NE(expected0[1], expected1[1]) << "the two object palettes are not the same ramp";
    EXPECT_EQ(expected0[3], expected1[3]) << "both make the same entry see-through";
    EXPECT_EQ(expected0[0], expected1[0]) << "and both draw the darkest colour darkest";

    // The font's two colours are the darkest and the see-through one, which is why one font palette
    // serves both object palettes.
    const std::array<retropp::Rgba8, 2> expectedFont{kShadeDarkest, kShadeTransparent};
    EXPECT_EQ(expectedFont[0], expected0[0]);
    EXPECT_EQ(expectedFont[1], expected0[3]);
}

// ── Test 4: NamesAreIdentityAndArriveWithoutEasing ────────────────────────────────────────────────
// A name is what the object is plus the tick it was placed on. The first half keeps two objects in
// one frame apart; the second stops the renderer easing an object from its previous position into
// its new one, which every object here needs because they all step a whole tile at a time.
TEST(SpriteBridge, NamesAreIdentityAndArriveWithoutEasing) {
    const TileAtlas atlas = labelledAtlas();

    GameContext game;
    game.spriteRenderer.slots[0] = SpriteSlot{.y = 0x40, .x = 0x30, .spriteId = SpriteId::DIGIT_3};
    game.spriteRenderer.slots[1] = SpriteSlot{.y = 0x50, .x = 0x38, .spriteId = SpriteId::L_0};
    kirpich::systems::renderCursors(game);

    const auto first = compose(game, TileSheet::GAMEPLAY, atlas, 7);
    ASSERT_FALSE(first.empty());

    // Unique within the frame, which the engine requires across every sprite layer.
    std::set<std::string> names;
    for (const auto& s : first) {
        names.insert(s.key.value);
    }
    EXPECT_EQ(names.size(), first.size()) << "every placed object is named once";

    // Standing still on the SAME tick is the same name — nothing has moved, so nothing is claimed.
    const auto again = compose(game, TileSheet::GAMEPLAY, atlas, 7);
    ASSERT_EQ(again.size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(again[i].key.value, first[i].key.value);
    }

    // A later tick renames everything, which is what makes the next placement arrive rather than
    // glide. The counter is what the alternation of a single bit could not do: a frame can span
    // several ticks, so a name must not come back around within one.
    const auto later = compose(game, TileSheet::GAMEPLAY, atlas, 8);
    ASSERT_EQ(later.size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_NE(later[i].key.value, first[i].key.value) << "entry " << i;
    }
    for (std::uint16_t span = 1; span <= 14; ++span) {
        const auto spanned = compose(game, TileSheet::GAMEPLAY, atlas,
                                     static_cast<std::uint16_t>(7 + span));
        EXPECT_NE(spanned[0].key.value, first[0].key.value)
            << "a frame spanning " << int{span} << " ticks must not reuse a name";
    }

    // The name follows the object, not the entry it landed in. Widening the first descriptor's art
    // pushes the second along the buffer; the second must keep its own name regardless.
    const std::string secondName = first[getSprite(SpriteId::DIGIT_3).parts.size()].key.value;
    GameContext shifted;
    shifted.spriteRenderer.slots[0] = SpriteSlot{.y = 0x40, .x = 0x30, .spriteId = SpriteId::BURAN};
    shifted.spriteRenderer.slots[1] = SpriteSlot{.y = 0x50, .x = 0x38, .spriteId = SpriteId::L_0};
    kirpich::systems::renderCursors(shifted);
    const auto moved = compose(shifted, TileSheet::GAMEPLAY, atlas, 7);
    const bool keptItsName = std::any_of(moved.begin(), moved.end(), [&](const retropp::Sprite& s) {
        return s.key.value == secondName;
    });
    EXPECT_TRUE(keptItsName) << "an object keeps its name when a neighbour's art changes width";

    // An entry the renderer never wrote is named for the entry, which is stable because the game
    // always fills those in at the same fixed place.
    GameContext direct;
    direct.engine.oam[0] = OamEntry{.y = 0x40, .x = 0x30, .tile = 0x80};
    const auto placed = compose(direct, TileSheet::GAMEPLAY, atlas, 3);
    ASSERT_EQ(placed.size(), 1u);
    EXPECT_EQ(placed[0].key.value, "oam0#3");
}

// ── Test 5: LayerShape ────────────────────────────────────────────────────────────────────────────
// The layer the objects are handed over in: one, above the background, the size of the screen and
// parked at its origin — the game never scrolls. Earlier entries draw over later ones, which is how
// the hardware broke a tie between two objects in the same place.
TEST(SpriteBridge, LayerShape) {
    const TileAtlas atlas = labelledAtlas();
    GameContext game;
    game.engine.oam[0] = OamEntry{.y = 0x40, .x = 0x30, .tile = 0x80};
    game.engine.oam[5] = OamEntry{.y = 0x40, .x = 0x38, .tile = 0x81};

    std::vector<retropp::Sprite> placed;
    composeSprites(game.engine, game.oamSources, TileSheet::GAMEPLAY, 0, atlas, placed);
    const retropp::DrawLayer layer = spriteLayer(placed);

    EXPECT_EQ(std::string_view(layer.key), kirpich::render::kSpriteLayerKey);
    EXPECT_EQ(layer.z, kirpich::render::kSpriteLayerZ);
    EXPECT_GT(layer.z, kirpich::render::kBackgroundLayerZ) << "objects draw over the background";
    EXPECT_EQ(layer.size, retropp::ViewportResolution::GameBoy.size());
    EXPECT_EQ(layer.scroll.x, 0);
    EXPECT_EQ(layer.scroll.y, 0);

    const auto* content = std::get_if<retropp::SpriteContent>(&layer.content);
    ASSERT_NE(content, nullptr) << "an object layer carries sprites";
    EXPECT_EQ(content->sprites.size(), placed.size());

    ASSERT_EQ(placed.size(), 2u);
    EXPECT_GT(placed[0].z, placed[1].z) << "the earlier entry draws on top";
}
