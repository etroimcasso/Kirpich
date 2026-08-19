#include "systems/type_b_ending.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <kirpich/game_state.h>
#include <kirpich/line_clear_kind.h>
#include <kirpich/sprite_id.h>

#include "data/music.h"          // MusicId
#include "data/playing_field.h"  // kPlayingFieldRows, kPlayingFieldCols
#include "data/scene_sprites.h"  // dancerSprites
#include "data/tilemaps.h"       // kScoreboardTilemap, kDancersTilemap
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/menu_screens.h"  // clearOamObjects, loadSceneSprites
#include "systems/scoring.h"          // printLineClearScores
#include "systems/sprite_renderer.h"  // renderSprites, renderActivePieceSprite, renderPreviewPieceSprite

namespace kirpich::systems {

namespace {

// Where the wipe animation starts when a screen load arms it.
constexpr std::uint8_t kWipeStartStep = 2;

// How long the scoreboard holds before the results count-up begins (128 frames, a little over two
// seconds), and how long the dance layout holds before the performers start moving (27 frames, which
// the original calls "almost half a second. Super random number").
constexpr std::uint8_t kScoreboardHoldFrames = 128;
constexpr std::uint8_t kDanceStartFrames = 27;

// The one frame of the dance that redraws instead of animating (tetris.asm:4786-4787).
constexpr std::uint8_t kDanceRedrawFrame = 20;

// The line count both endings leave behind. The original stores it packed-decimal and writes the byte
// $25, which is twenty-five; the port's count is a plain decimal number.
constexpr std::uint16_t kTypeBEndingLines = 25;

// The four scoreboard rows: which line-clear kind each one prints, and the field cell its digits start
// at (tetris.asm:4627-4638, destinations $C827 / $C887 / $C8E7 / $C947).
struct ScoreboardRow {
    LineClearKind kind;
    std::uint8_t  fieldRow;
    std::uint8_t  fieldCol;
};
constexpr std::array<ScoreboardRow, 4> kScoreboardRows{{
    { .kind = LineClearKind::SINGLE, .fieldRow = 1,  .fieldCol = 5 },
    { .kind = LineClearKind::DOUBLE, .fieldRow = 4,  .fieldCol = 5 },
    { .kind = LineClearKind::TRIPLE, .fieldRow = 7,  .fieldCol = 5 },
    { .kind = LineClearKind::TETRIS, .fieldRow = 10, .fieldCol = 5 },
}};

// The ten performers of the dance occupy the first ten sprite slots, and slots 6 and 7 — the jumping
// cossack and the dancer beside him — are drawn from the second object palette (tetris.asm:4730-4734).
constexpr std::size_t kDancerSlotCount = 10;
constexpr std::array<std::size_t, 2> kSecondPaletteSlots{ 6, 7 };

// How fast each performer moves: one animation period per slot (tetris.asm:4776-4777). Each is written
// to both halves of the slot's animation pair, so the ten of them never fall into step.
constexpr std::array<std::uint8_t, kDancerSlotCount> kDancerAnimationPeriods{
    0x1C, 0x0F, 0x1E, 0x32, 0x20, 0x18, 0x26, 0x1D, 0x28, 0x2B,
};

// One more dancer than the starting garbage height — except at height 5, which reveals all ten rather
// than six (tetris.asm:4749-4755).
constexpr std::uint8_t kAllDancersHeight = 5;

// The jumping cossack's two positions: down on his first frame, ten pixels higher on his second
// (tetris.asm:4834, :4840).
constexpr std::uint8_t kCossackDownY = 0x67;
constexpr std::uint8_t kCossackUpY = 0x5D;

// LoadPlayingFieldTilemap (tetris.asm:6434-6457): copy a field-shaped screen into the board at the
// field's own top-left cell, then arm the wipe. The order is the contract — the original writes the
// wipe step only on reaching the stored screen's terminator, so every cell is in place before the
// animation starts. This is the mirror of the gameplay session's field fill, which arms first.
void loadPlayingFieldTilemap(GameContext& game, const auto& tilemap) {
    for (std::size_t row = 0; row < kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            game.field.fieldCell(row, col) = tilemap[row][col];
        }
    }
    game.flow.wipeCounter = kWipeStartStep;
}

// How many performers a round started at `startHeight` reveals.
std::uint8_t visibleDancerCount(std::uint8_t startHeight) {
    if (startHeight == kAllDancersHeight) {
        return static_cast<std::uint8_t>(kDancerSlotCount);
    }
    return static_cast<std::uint8_t>(startHeight + 1);
}

// Flip a performer to its other frame. Every performer's two frames are a consecutive pair, so toggling
// the low bit of the sprite id moves between them in either direction (tetris.asm:4800-4806).
void toggleDancerFrame(SpriteSlot& slot) {
    slot.spriteId = static_cast<SpriteId>(static_cast<std::uint8_t>(slot.spriteId) ^ 1u);

    // Only the jumping cossack moves vertically, and the test is on the sprite id, so the other nine
    // performers flip in place (tetris.asm:4807-4810, :4831-4841).
    if (slot.spriteId == SpriteId::JUMPING_COSSACK_1) {
        slot.y = kCossackDownY;
    } else if (slot.spriteId == SpriteId::JUMPING_COSSACK_2) {
        slot.y = kCossackUpY;
    }
}

}  // namespace

void typeBVictoryJingle(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }

    loadPlayingFieldTilemap(game, kScoreboardTilemap);

    // A level-0 round skips the whole print block: the drawn screen already carries the level-0 values
    // (tetris.asm:4624-4626). The score clear belongs to that arm too — the original dirties the score
    // bytes only by printing through them.
    if (game.flow.typeBLevel != 0) {
        for (const ScoreboardRow& row : kScoreboardRows) {
            printLineClearScores(game, row.kind, row.fieldRow, row.fieldCol);
        }
        game.engine.score = 0;
    }

    game.flow.timer1 = kScoreboardHoldFrames;
    game.spriteRenderer.slots[kActivePieceSlot].hidden = true;
    game.spriteRenderer.slots[kPreviewPieceSlot].hidden = true;
    renderActivePieceSprite(game);   // (:4653) — take both pieces off the scoreboard
    renderPreviewPieceSprite(game);  // (:4654)
    game.audioCues.resetRequested = true;
    game.flow.lines = kTypeBEndingLines;
    game.flow.gameState = GameState::INIT_TYPE_B_SCOREBOARD;
}

void initBonusEnding(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }

    loadPlayingFieldTilemap(game, kDancersTilemap);
    clearOamObjects(game);
    loadSceneSprites(game.spriteRenderer, dancerSprites());

    for (const std::size_t slot : kSecondPaletteSlots) {
        game.spriteRenderer.slots[slot].palette1 = true;
    }

    // Both halves of the animation pair take the same period, so each performer restarts its own
    // countdown from its own value (tetris.asm:4735-4748).
    for (std::size_t i = 0; i < kDancerSlotCount; ++i) {
        game.spriteRenderer.slots[i].animCounter = kDancerAnimationPeriods[i];
        game.spriteRenderer.slots[i].animReload = kDancerAnimationPeriods[i];
    }

    const std::uint8_t visible = visibleDancerCount(game.flow.typeBStartHeight);
    for (std::size_t i = 0; i < visible; ++i) {
        game.spriteRenderer.slots[i].hidden = false;
    }

    // The jingle for this round's starting height: height 0 gets the first, height 5 the sixth
    // (tetris.asm:4764-4766).
    game.audioCues.music = static_cast<MusicId>(
        static_cast<std::uint8_t>(MusicId::TYPE_B_JINGLE_1) + game.flow.typeBStartHeight);

    game.flow.lines = kTypeBEndingLines;
    game.flow.timer1 = kDanceStartFrames;
    game.flow.gameState = GameState::DANCERS;
}

void dancers(GameContext& game, const MusicPlayingQuery& musicPlaying) {
    // At exactly 20 the original redraws the performers and does nothing else (tetris.asm:4785-4787,
    // via Label_1E3B) — the one frame that draws the troupe without advancing anyone's animation.
    if (game.flow.timer1 == kDanceRedrawFrame) {
        renderSprites(game, kDancerSlotCount);
        return;
    }
    if (game.flow.timer1 != 0) {
        return;
    }

    for (std::size_t i = 0; i < kDancerSlotCount; ++i) {
        SpriteSlot& slot = game.spriteRenderer.slots[i];

        // A counter already at zero wraps to 255 rather than firing, exactly as the original's
        // decrement does (tetris.asm:4795-4796).
        if (--slot.animCounter != 0) {
            continue;
        }
        slot.animCounter = slot.animReload;
        toggleDancerFrame(slot);
    }

    renderSprites(game, kDancerSlotCount);  // (:4816-4817) — the whole troupe, every animated frame

    // The dance runs until the jingle ends (tetris.asm:4818-4820).
    if (musicPlaying && musicPlaying()) {
        return;
    }

    clearOamObjects(game);
    game.flow.gameState = game.flow.typeBStartHeight == kAllDancersHeight
                              ? GameState::INIT_BURAN
                              : GameState::TYPE_B_VICTORY_JINGLE;
}

void installTypeBEndingHandlers(GameStateDispatcher& dispatcher, MusicPlayingQuery musicPlaying) {
    dispatcher.setHandler(GameState::TYPE_B_VICTORY_JINGLE, typeBVictoryJingle);
    dispatcher.setHandler(GameState::INIT_TYPE_B_BONUS, initBonusEnding);
    dispatcher.setHandler(GameState::DANCERS, [musicPlaying = std::move(musicPlaying)](GameContext& g) {
        dancers(g, musicPlaying);
    });
}

}  // namespace kirpich::systems
