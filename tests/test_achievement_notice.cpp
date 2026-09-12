// The end-of-round notice — behavioral tests over its logic (src/systems/achievement_notice.h) and
// its components (src/render/achievements/banner.h, notice.h), plus the two round exits that route
// through it.
//
// Device-free. The logic is pure state over the game aggregate, and the components are pure functions
// returning primitives, so the atlas here is a plain value with distinguishable handles. The notice is
// the port's own screen, so every asserted value comes from its stated contract; the one exception is
// the Type C rocket case, which asserts the port's correction to what the cartridge does.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/bounded_vec.h"
#include "data/sfx.h"  // SquareSfxId
#include "render/achievements/badge.h"  // badgeExtent
#include "render/achievements/banner.h"
#include "render/achievements/notice.h"
#include "render/achievements/notice_layout.h"
#include "render/glyphs.h"
#include "retropp/input.h"
#include "state/achievement_state.h"
#include "systems/achievement_notice.h"
#include "systems/achievements.h"
#include "systems/boot.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"
#include "systems/launch_scenes.h"

namespace {

using kirpich::AchievementDate;
using kirpich::AchievementId;
using kirpich::AchievementNoticeState;
using kirpich::Action;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::render::TileAtlas;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;
using kirpich::systems::NowDate;

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held    = game.joypad.pressed;
}

constexpr std::uint8_t kPlayerRamp = 7;

TileAtlas makeAtlas() {
    TileAtlas atlas{
        .font             = static_cast<retropp::AtlasId>(11),
        .copyrightTitle   = static_cast<retropp::AtlasId>(22),
        .gameplay         = static_cast<retropp::AtlasId>(33),
        .multiplayerBuran = static_cast<retropp::AtlasId>(44),
    };
    for (std::size_t ramp = 0; ramp < kirpich::render::kShadeRampCount; ++ramp) {
        const auto id = [ramp](int kind) {
            return static_cast<retropp::PaletteId>(1000 * kind + static_cast<int>(ramp));
        };
        atlas.palettes[ramp].font       = id(1);
        atlas.palettes[ramp].content    = id(2);
        atlas.palettes[ramp].fontDim    = id(3);
        atlas.palettes[ramp].fontSprite = id(4);
        atlas.palettes[ramp].sprite0    = id(5);
        atlas.palettes[ramp].sprite1    = id(6);
        atlas.palettes[ramp].fontSpriteDim = id(7);
        atlas.palettes[ramp].spriteDim     = id(8);
    }
    return atlas;
}

const TileAtlas kAtlas = makeAtlas();

NowDate fixedDate() {
    return [] { return AchievementDate{.year = 2026, .month = 9, .day = 11}; };
}

// Play a Type A round worth `score` through the real seams and run the check, so the queue below is
// filled the way a finished round fills it rather than by hand.
void playTypeARound(GameContext& game, std::uint32_t score) {
    game.flow.gameType = GameType::TYPE_A;
    game.engine.score  = score;
    kirpich::beginAchievementRound(game.achievements);
    kirpich::noteRoundConcluded(game.achievements, /*wonTypeB=*/false);
    kirpich::systems::evaluateRoundEnd(game, fixedDate());
}

std::span<const retropp::Sprite> spritesOf(const retropp::DrawLayer& layer) {
    return std::get<retropp::SpriteContent>(layer.content).sprites;
}

// Which sprites in a run are glyphs, in the order they were emitted. Glyph keys carry where the
// character sits, which is how a text run is told apart from the badge art beside it.
std::vector<retropp::Sprite> glyphsOf(std::span<const retropp::Sprite> sprites) {
    std::vector<retropp::Sprite> out;
    for (const retropp::Sprite& s : sprites) {
        if (std::string_view{s.key.value}.starts_with("ach-g-")) out.push_back(s);
    }
    return out;
}

// The glyph run a piece of text would produce at a place, for comparing against what a component
// actually emitted.
std::vector<retropp::Sprite> expectedGlyphs(std::string_view text, int x, int y) {
    const kirpich::render::Sprites run =
        kirpich::render::Glyphs(text, x, y, kirpich::render::kNoticeGlyphPitch, kAtlas, kPlayerRamp);
    return {run.begin(), run.end()};
}

}  // namespace

// ── The queue's container ─────────────────────────────────────────────────────────────────────────

// (0) The queue is a BoundedVec filled one id at a time, which is what push_back was added for, so its
// append is pinned here beside its first consumer: the count follows, the order is kept, and equality
// still reflects only what is live.
TEST(AchievementNotice, TheQueuesContainerAppendsInOrder) {
    kirpich::BoundedVec<AchievementId, 4> queue;
    EXPECT_TRUE(queue.empty());

    queue.push_back(AchievementId::STACKING_UP);
    queue.push_back(AchievementId::GETTING_WARM);

    ASSERT_EQ(queue.size(), 2u);
    EXPECT_EQ(queue[0], AchievementId::STACKING_UP);
    EXPECT_EQ(queue[1], AchievementId::GETTING_WARM);

    const kirpich::BoundedVec<AchievementId, 4> same{AchievementId::STACKING_UP,
                                                     AchievementId::GETTING_WARM};
    EXPECT_EQ(queue, same) << "an appended vec equals the same elements braced in";
}

// ── The exit ──────────────────────────────────────────────────────────────────────────────────────

// (1) A round that earned nothing goes where it was going, and the notice state is left exactly as it
// was found - asserted as a whole struct, so a stray write to any field fails the case.
TEST(AchievementNotice, ARoundThatEarnedNothingPassesStraightThrough) {
    GameContext game;
    playTypeARound(game, /*score=*/0);

    const AchievementNoticeState before = game.achievementNotice;
    ASSERT_TRUE(before.pending.empty()) << "a scoreless round earns nothing";

    EXPECT_EQ(kirpich::systems::achievementNoticeExit(game, GameState::INIT_TYPE_B_DIFFICULTY),
              GameState::INIT_TYPE_B_DIFFICULTY);
    EXPECT_EQ(game.achievementNotice, before);
}

// (2) A round that earned something is held: the destination is kept, the queue is read from its
// start, and the player is sent to the notice.
TEST(AchievementNotice, ARoundThatEarnedSomethingIsHeldAtTheNotice) {
    GameContext game;
    playTypeARound(game, /*score=*/5000);
    ASSERT_FALSE(game.achievementNotice.pending.empty());

    game.achievementNotice.shown = 3;  // whatever a previous notice left

    EXPECT_EQ(kirpich::systems::achievementNoticeExit(game, GameState::INIT_TYPE_C_DIFFICULTY),
              GameState::ACHIEVEMENT_NOTICE);
    EXPECT_EQ(game.achievementNotice.resume, GameState::INIT_TYPE_C_DIFFICULTY);
    EXPECT_EQ(game.achievementNotice.shown, 0) << "the queue is shown from its start";
}

// (3) The check queues exactly what it just awarded, in the set's order, and never an achievement the
// player already had.
TEST(AchievementNotice, TheCheckQueuesOnlyWhatItJustAwarded) {
    GameContext game;

    // A big first round crosses several score thresholds at once.
    playTypeARound(game, /*score=*/30000);
    const std::vector<AchievementId> first{game.achievementNotice.pending.begin(),
                                           game.achievementNotice.pending.end()};
    ASSERT_GE(first.size(), 2u) << "one round can cross more than one threshold";
    EXPECT_EQ(first[0], AchievementId::GETTING_WARM);
    EXPECT_EQ(first[1], AchievementId::STACKING_UP);
    for (const AchievementId id : first) {
        EXPECT_TRUE(game.achievements.unlocked[kirpich::achievementIndex(id)].unlocked)
            << "everything queued is also stamped";
    }

    // The same round again earns nothing new, so there is nothing to announce.
    playTypeARound(game, /*score=*/30000);
    EXPECT_TRUE(game.achievementNotice.pending.empty())
        << "an achievement already earned is not announced twice";
}

// ── Stepping through what was earned ──────────────────────────────────────────────────────────────

// (4) A press steps one badge at a time, and a frame with no press changes nothing.
TEST(AchievementNotice, EachPressStepsToTheNextBadge) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementNotice(dispatcher);

    GameContext game;
    playTypeARound(game, /*score=*/30000);
    game.flow.gameState = kirpich::systems::achievementNoticeExit(
        game, GameState::INIT_TYPE_A_DIFFICULTY);
    ASSERT_EQ(game.flow.gameState, GameState::ACHIEVEMENT_NOTICE);

    const std::vector<AchievementId> queued{game.achievementNotice.pending.begin(),
                                            game.achievementNotice.pending.end()};
    ASSERT_GE(queued.size(), 2u);

    EXPECT_EQ(kirpich::systems::achievementOnNotice(game), queued[0]);

    dispatcher.tick(game, retropp::ActionSet{});  // no press
    EXPECT_EQ(game.flow.gameState, GameState::ACHIEVEMENT_NOTICE);
    EXPECT_EQ(kirpich::systems::achievementOnNotice(game), queued[0]);

    for (std::size_t i = 1; i < queued.size(); ++i) {
        dispatcher.tick(game, actionSet({Action::Confirm}));
        EXPECT_EQ(game.flow.gameState, GameState::ACHIEVEMENT_NOTICE)
            << "the notice holds until the last badge has been seen";
        EXPECT_EQ(kirpich::systems::achievementOnNotice(game), queued[i]);
        dispatcher.tick(game, retropp::ActionSet{});  // release, so the next press is an edge
    }
}

// (4b) Every banner arrives on the level-up cue, and the press that ends the notice does not cue -
// nothing arrives on it. The mailbox is cleared before each step the way the audio tick drains it, so
// every assertion reads what that frame put there rather than what an earlier one left.
TEST(AchievementNotice, EveryBannerArrivesOnTheLevelUpCue) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementNotice(dispatcher);

    GameContext game;
    playTypeARound(game, /*score=*/30000);

    game.audioCues.square = kirpich::SquareSfxId::NONE;
    game.flow.gameState =
        kirpich::systems::achievementNoticeExit(game, GameState::INIT_TYPE_A_DIFFICULTY);
    ASSERT_EQ(game.flow.gameState, GameState::ACHIEVEMENT_NOTICE);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::LEVEL_UP)
        << "the first banner cues as it arrives";

    const std::size_t queued = game.achievementNotice.pending.size();
    ASSERT_GE(queued, 2u);

    for (std::size_t i = 1; i < queued; ++i) {
        dispatcher.tick(game, retropp::ActionSet{});  // release, so the next press is an edge
        game.audioCues.square = kirpich::SquareSfxId::NONE;
        dispatcher.tick(game, actionSet({Action::Confirm}));
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::LEVEL_UP)
            << "and so does every later one, banner " << i;
    }

    // The press past the last badge shows no banner, so it cues nothing.
    dispatcher.tick(game, retropp::ActionSet{});
    game.audioCues.square = kirpich::SquareSfxId::NONE;
    dispatcher.tick(game, actionSet({Action::Confirm}));
    EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_A_DIFFICULTY);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE)
        << "leaving the notice is not an arrival";
}

// (5) The press past the last badge empties the queue and releases the round to where it was headed.
// Start does it as readily as Confirm, because both dismiss the screen before this one.
TEST(AchievementNotice, TheLastPressReleasesTheRoundToItsDestination) {
    for (const Action button : {Action::Confirm, Action::Start}) {
        GameStateDispatcher dispatcher;
        kirpich::systems::installAchievementNotice(dispatcher);

        GameContext game;
        playTypeARound(game, /*score=*/5000);
        game.flow.gameState = kirpich::systems::achievementNoticeExit(
            game, GameState::INIT_TYPE_C_DIFFICULTY);
        ASSERT_EQ(game.flow.gameState, GameState::ACHIEVEMENT_NOTICE);
        ASSERT_EQ(game.achievementNotice.pending.size(), 1u);

        dispatcher.tick(game, actionSet({button}));

        EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_C_DIFFICULTY);
        EXPECT_EQ(game.achievementNotice, AchievementNoticeState{})
            << "nothing is left queued for the next round to show";
    }
}

// (6) A soft reset clears the queue: a reset taken while a round is in flight cannot leave badges to
// be announced after the next one. What the player has earned survives it, as it always has.
TEST(AchievementNotice, ASoftResetClearsTheQueueAndKeepsWhatWasEarned) {
    GameContext game;
    playTypeARound(game, /*score=*/5000);
    ASSERT_FALSE(game.achievementNotice.pending.empty());

    kirpich::systems::softReset(game);

    EXPECT_EQ(game.achievementNotice, AchievementNoticeState{});
    EXPECT_TRUE(game.achievements.unlocked[kirpich::achievementIndex(AchievementId::GETTING_WARM)]
                    .unlocked)
        << "the reset keeps what was earned; only what is still to be shown goes";
}

// ── The two round exits ───────────────────────────────────────────────────────────────────────────

// (7) The game-over screen hands the seam the destination for the mode just played, and takes back
// whatever the seam returns.
TEST(AchievementNotice, TheGameOverScreenRoutesItsExitThroughTheSeam) {
    struct Case {
        bool      multiplayer;
        GameType  type;
        GameState destination;
    };
    const Case cases[] = {
        {false, GameType::TYPE_A, GameState::INIT_TYPE_A_DIFFICULTY},
        {false, GameType::TYPE_B, GameState::INIT_TYPE_B_DIFFICULTY},
        {false, GameType::TYPE_C, GameState::INIT_TYPE_C_DIFFICULTY},
        {true, GameType::TYPE_A, GameState::INIT_2P_DIFFICULTY},
    };

    for (const Case& c : cases) {
        GameState offered = GameState::COPYRIGHT_SCREEN;  // a value the screen would never pick

        GameContext game;
        game.flow.gameState            = GameState::GAME_OVER_SCREEN;
        game.multiplayer.isMultiplayer = c.multiplayer;
        game.flow.gameType             = c.type;
        press(game, {Action::Start});

        kirpich::systems::gameOverScreen(game, [&offered](GameContext&, GameState destination) {
            offered = destination;
            return GameState::ACHIEVEMENT_NOTICE;  // the seam's answer is what stands
        });

        EXPECT_EQ(offered, c.destination) << "the seam is offered the mode's own picker";
        EXPECT_EQ(game.flow.gameState, GameState::ACHIEVEMENT_NOTICE);
    }
}

// (8) The rocket scene's exit does the same - and picks the picker for the mode that flew it. The
// cartridge names Type A here because Type A is the only mode it has that earns a rocket; this port
// gives Type C the same boundaries, so a Type C round has to come back to its own picker.
TEST(AchievementNotice, TheRocketExitReturnsToThePickerForTheModeThatFlewIt) {
    struct Case {
        GameType  type;
        GameState destination;
    };
    const Case cases[] = {
        {GameType::TYPE_A, GameState::INIT_TYPE_A_DIFFICULTY},
        {GameType::TYPE_C, GameState::INIT_TYPE_C_DIFFICULTY},
    };

    for (const Case& c : cases) {
        {  // with no seam, the scene goes straight to that picker
            GameContext game;
            game.flow.gameType = c.type;
            kirpich::systems::endOfBonusScene(game);
            EXPECT_EQ(game.flow.gameState, c.destination);
        }
        {  // and with one, the seam is offered the same destination
            GameState offered = GameState::COPYRIGHT_SCREEN;

            GameContext game;
            game.flow.gameType = c.type;
            kirpich::systems::endOfBonusScene(
                game, [&offered](GameContext&, GameState destination) {
                    offered = destination;
                    return GameState::ACHIEVEMENT_NOTICE;
                });

            EXPECT_EQ(offered, c.destination);
            EXPECT_EQ(game.flow.gameState, GameState::ACHIEVEMENT_NOTICE);
        }
    }
}

// ── The picture ───────────────────────────────────────────────────────────────────────────────────

// (9) The banner is the badge, the title beside it, and the description wrapped under the title -
// every glyph where the layout puts it, read back from what the component returns.
TEST(AchievementNotice, TheBannerIsABadgeATitleAndTheDescriptionUnderIt) {
    const AchievementId id  = AchievementId::GETTING_WARM;
    const auto&         def = kirpich::systems::achievementDef(id);

    const kirpich::render::Sprites banner = kirpich::render::AchievementBanner(
        id, kirpich::render::kNoticeBadgeX, kirpich::render::kNoticeBadgeY, kAtlas, kPlayerRamp);

    const int textX =
        kirpich::render::noticeTextX(kirpich::render::badgeExtent(id).width);
    EXPECT_GT(textX, kirpich::render::kNoticeBadgeX + kirpich::render::badgeExtent(id).width)
        << "the text clears the badge's own art";

    std::vector<retropp::Sprite> expected =
        expectedGlyphs(def.title, textX, kirpich::render::kNoticeBadgeY);

    int line = kirpich::render::kNoticeBadgeY + kirpich::render::kNoticeTitleGap;
    const std::vector<std::string_view> body =
        kirpich::render::wrapText(def.description, kirpich::render::noticeTextCells(textX));
    ASSERT_FALSE(body.empty());
    for (const std::string_view run : body) {
        const std::vector<retropp::Sprite> glyphs = expectedGlyphs(run, textX, line);
        expected.insert(expected.end(), glyphs.begin(), glyphs.end());
        line += kirpich::render::kNoticeLineStep;
    }

    const std::vector<retropp::Sprite> drawn = glyphsOf(banner);
    ASSERT_EQ(drawn.size(), expected.size());
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        EXPECT_EQ(std::string_view{drawn[i].key.value}, std::string_view{expected[i].key.value});
        EXPECT_EQ(drawn[i].x, expected[i].x);
        EXPECT_EQ(drawn[i].y, expected[i].y);
        EXPECT_EQ(drawn[i].tile, expected[i].tile);
    }

    EXPECT_GT(banner.size(), drawn.size()) << "the badge's own art is there beside the text";
}

// (10) A hidden achievement that has just been earned shows what it was: being earned is what reveals
// it, so the banner never draws the placeholder the list screen uses for one still locked.
TEST(AchievementNotice, AJustEarnedHiddenBadgeShowsItsRealTitle) {
    AchievementId hidden{};
    bool          found = false;
    for (const auto& def : kirpich::systems::achievementSet()) {
        if (def.hidden) {
            hidden = def.id;
            found  = true;
            break;
        }
    }
    ASSERT_TRUE(found) << "the set has hidden achievements";

    const kirpich::render::Sprites banner = kirpich::render::AchievementBanner(
        hidden, kirpich::render::kNoticeBadgeX, kirpich::render::kNoticeBadgeY, kAtlas, kPlayerRamp);

    const int textX =
        kirpich::render::noticeTextX(kirpich::render::badgeExtent(hidden).width);
    const std::vector<retropp::Sprite> expected = expectedGlyphs(
        kirpich::systems::achievementDef(hidden).title, textX, kirpich::render::kNoticeBadgeY);
    ASSERT_FALSE(expected.empty());

    const std::vector<retropp::Sprite> drawn = glyphsOf(banner);
    ASSERT_GE(drawn.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(drawn[i].tile, expected[i].tile) << "its own title, not a placeholder";
    }
}

// (11) The screen is a backdrop and the banner on top of it, under keys that are the objects' own.
TEST(AchievementNotice, TheScreenIsABackdropAndABanner) {
    const AchievementId id = AchievementId::GETTING_WARM;

    const kirpich::render::Layers layers =
        kirpich::render::AchievementNotice(id, kAtlas, kPlayerRamp);

    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(std::string_view{layers[0].key.value}, "ach-notice-backdrop");
    EXPECT_EQ(layers[0].z, 0);
    EXPECT_TRUE(std::holds_alternative<retropp::TileContent>(layers[0].content))
        << "the backdrop is background tiles";

    EXPECT_EQ(std::string_view{layers[1].key.value}, "ach-notice");
    EXPECT_GT(layers[1].z, layers[0].z) << "the banner is above the backdrop";

    const kirpich::render::Sprites banner = kirpich::render::AchievementBanner(
        id, kirpich::render::kNoticeBadgeX, kirpich::render::kNoticeBadgeY, kAtlas, kPlayerRamp);
    EXPECT_EQ(spritesOf(layers[1]).size(), banner.size());
}

// (12) The notice draws on its own state and no other.
TEST(AchievementNotice, TheScreenIsShownOnItsOwnStateAlone) {
    for (std::size_t raw = 0; raw < kirpich::systems::kGameStateCount; ++raw) {
        const auto state = static_cast<GameState>(raw);
        EXPECT_EQ(kirpich::render::achievementNoticeShown(state),
                  state == GameState::ACHIEVEMENT_NOTICE)
            << "state " << raw;
    }
}
