// Sprite scene lists: the OAM object-placement tables each scripted scene draws.
//
// The engine tables (src/data/scene_sprites.h) are swept in full against the parser-emitted fixture
// (tests/fixtures/scene_sprites_expected.h): the fixture holds the ROM's raw object bytes, and the
// sweep here re-derives each SceneSprite from those bytes and compares to the accessors, so a defect
// in either the typed surface or the parser cannot hide. The remaining tests pin the SpriteId links,
// the attribute bits, the hidden flags, and the two piece templates. Expectations come from
// docs/contracts/sprite-scenes.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

#include <kirpich/sprite_id.h>

#include "data/scene_sprites.h"
#include "fixtures/scene_sprites_expected.h"

namespace {

using kirpich::SceneSprite;
using kirpich::SpriteId;
using kirpich::fixtures::kExpectedSceneSpriteBytes;
using kirpich::fixtures::kExpectedSceneTables;

// The accessor tables in the same order as kExpectedSceneTables. The two single-object templates
// appear as one-element spans so the full-corpus sweep can treat every table uniformly.
const std::array<std::span<const SceneSprite>, 13> kAccessorTables{{
    kirpich::configScreenSprites(),
    kirpich::typeADifficultySprites(),
    kirpich::typeBDifficultySprites(),
    kirpich::twoPlayerHeightSprites(),
    kirpich::marioVictorySprites(),
    kirpich::luigiVictorySprites(),
    kirpich::marioDefeatSprites(),
    kirpich::luigiDefeatSprites(),
    kirpich::dancerSprites(),
    kirpich::buranLaunchSprites(),
    kirpich::rocketLaunchSprites(),
    std::span<const SceneSprite>(&kirpich::activePieceSprite(), 1),
    std::span<const SceneSprite>(&kirpich::previewPieceSprite(), 1),
}};

// Re-derive a SceneSprite from six raw object bytes, the same unpack the parser applies.
SceneSprite decode(const std::uint8_t* b) {
    return SceneSprite{
        .hidden = (b[0] == 0x80),
        .y = b[1],
        .x = b[2],
        .sprite = static_cast<SpriteId>(b[3]),
        .behindBg = (b[4] & 0x80) != 0,
        .xflip = (b[5] & 0x20) != 0,
    };
}

// The moment-of-truth: every table, every object, re-derived from the raw fixture and compared to
// the accessor. 13 tables / 35 objects.
TEST(SceneSprites, SceneCorpusFullSweep) {
    std::size_t total = 0;
    for (std::size_t i = 0; i < kExpectedSceneTables.size(); ++i) {
        const auto& row = kExpectedSceneTables[i];
        const std::span<const SceneSprite> table = kAccessorTables[i];
        ASSERT_EQ(table.size(), row.record_count) << "table " << i;
        for (std::size_t r = 0; r < row.record_count; ++r) {
            const std::size_t base = row.byte_offset + r * row.bytes_per_record;
            const std::uint8_t* b = &kExpectedSceneSpriteBytes[base];
            if (row.terminated) {
                EXPECT_EQ(b[6], 0x00) << "table " << i << " base OAM flags byte";
                EXPECT_EQ(b[7], 0xFF) << "table " << i << " terminator";
            }
            EXPECT_EQ(table[r], decode(b)) << "table " << i << " record " << r;
            ++total;
        }
    }
    EXPECT_EQ(total, 35u);
}

TEST(SceneSprites, SceneTableCounts) {
    EXPECT_EQ(kirpich::configScreenSprites().size(), 2u);
    EXPECT_EQ(kirpich::typeADifficultySprites().size(), 1u);
    EXPECT_EQ(kirpich::typeBDifficultySprites().size(), 2u);
    EXPECT_EQ(kirpich::twoPlayerHeightSprites().size(), 2u);
    EXPECT_EQ(kirpich::marioVictorySprites().size(), 3u);
    EXPECT_EQ(kirpich::luigiVictorySprites().size(), 3u);
    EXPECT_EQ(kirpich::marioDefeatSprites().size(), 2u);
    EXPECT_EQ(kirpich::luigiDefeatSprites().size(), 2u);
    EXPECT_EQ(kirpich::dancerSprites().size(), 10u);
    EXPECT_EQ(kirpich::buranLaunchSprites().size(), 3u);
    EXPECT_EQ(kirpich::rocketLaunchSprites().size(), 3u);
}

TEST(SceneSprites, SpriteIdLinks) {
    EXPECT_EQ(kirpich::marioVictorySprites()[0].sprite, SpriteId::JUMPING_LARGE_MARIO_1);
    EXPECT_EQ(kirpich::luigiVictorySprites()[0].sprite, SpriteId::JUMPING_LARGE_LUIGI_1);
    EXPECT_EQ(kirpich::marioDefeatSprites()[0].sprite, SpriteId::CRYING_LARGE_MARIO_1);
    EXPECT_EQ(kirpich::buranLaunchSprites()[0].sprite, SpriteId::BURAN);
    EXPECT_EQ(kirpich::rocketLaunchSprites()[0].sprite, SpriteId::ROCKET_L);
    EXPECT_EQ(kirpich::configScreenSprites()[0].sprite, SpriteId::A_TYPE);
    EXPECT_EQ(kirpich::typeADifficultySprites()[0].sprite, SpriteId::DIGIT_0);  // the difficulty digit
    EXPECT_EQ(kirpich::typeBDifficultySprites()[0].sprite, SpriteId::DIGIT_0);
    EXPECT_EQ(kirpich::twoPlayerHeightSprites()[0].sprite, SpriteId::DIGIT_1);

    // The ending dance, in placement order.
    const std::array<SpriteId, 10> kDancers{{
        SpriteId::VIOLINIST_1, SpriteId::GUITARIST_1, SpriteId::CELLIST_1, SpriteId::BIG_DRUM_1,
        SpriteId::FLUTIST_PAIR_1, SpriteId::BAYAN_1, SpriteId::JUMPING_COSSACK_1, SpriteId::DANCER_1,
        SpriteId::KOKOSHNIK_WOMAN_1, SpriteId::KOKOSHNIK_WOMAN_2,
    }};
    const auto dancers = kirpich::dancerSprites();
    for (std::size_t i = 0; i < kDancers.size(); ++i) {
        EXPECT_EQ(dancers[i].sprite, kDancers[i]) << "dancer " << i;
    }
}

TEST(SceneSprites, AttributePins) {
    // xflip mirrors the second object of each victory pair and the second launch-smoke plume.
    EXPECT_FALSE(kirpich::marioVictorySprites()[0].xflip);
    EXPECT_TRUE(kirpich::marioVictorySprites()[1].xflip);
    EXPECT_FALSE(kirpich::marioVictorySprites()[2].xflip);
    EXPECT_TRUE(kirpich::luigiVictorySprites()[1].xflip);
    EXPECT_TRUE(kirpich::buranLaunchSprites()[2].xflip);
    EXPECT_TRUE(kirpich::rocketLaunchSprites()[2].xflip);

    // behindBg is set on the victory/defeat characters, clear on dancers and launch objects.
    EXPECT_TRUE(kirpich::marioVictorySprites()[0].behindBg);
    EXPECT_TRUE(kirpich::luigiDefeatSprites()[0].behindBg);
    EXPECT_FALSE(kirpich::dancerSprites()[0].behindBg);
    EXPECT_FALSE(kirpich::rocketLaunchSprites()[0].behindBg);
    EXPECT_FALSE(kirpich::configScreenSprites()[0].behindBg);
}

TEST(SceneSprites, HiddenPins) {
    for (const SceneSprite& d : kirpich::dancerSprites()) {
        EXPECT_TRUE(d.hidden);
    }
    // The launch smoke plumes start hidden; the shuttle/rocket itself is visible.
    EXPECT_FALSE(kirpich::buranLaunchSprites()[0].hidden);
    EXPECT_TRUE(kirpich::buranLaunchSprites()[1].hidden);
    EXPECT_TRUE(kirpich::buranLaunchSprites()[2].hidden);
    EXPECT_FALSE(kirpich::rocketLaunchSprites()[0].hidden);
    EXPECT_TRUE(kirpich::rocketLaunchSprites()[1].hidden);
    // Victory characters are visible.
    EXPECT_FALSE(kirpich::marioVictorySprites()[0].hidden);
}

TEST(SceneSprites, PieceTemplatePins) {
    const SceneSprite& active = kirpich::activePieceSprite();
    const SceneSprite& preview = kirpich::previewPieceSprite();
    for (const SceneSprite* t : {&active, &preview}) {
        EXPECT_EQ(t->sprite, SpriteId::L_0);   // runtime placeholder
        EXPECT_TRUE(t->behindBg);
        EXPECT_FALSE(t->hidden);
        EXPECT_FALSE(t->xflip);
    }
    EXPECT_EQ(active.y, 0x18);
    EXPECT_EQ(active.x, 0x3F);
    EXPECT_EQ(preview.y, 0x80);
    EXPECT_EQ(preview.x, 0x8F);
}

}  // namespace
