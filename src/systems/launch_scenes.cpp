#include "systems/launch_scenes.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/sprite_id.h>

#include "data/music.h"          // MusicId
#include "data/scene_sprites.h"  // buranLaunchSprites, rocketLaunchSprites
#include "data/sfx.h"            // NoiseSfxId, SquareSfxId
#include "data/tilemaps.h"       // kBuranBackdropTilemap, the tower strips, kCongratulationsTilemap
#include "state/display_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"         // fillPlayingFieldAndWipe
#include "systems/line_clear.h"       // clearLineClearsList
#include "systems/menu_screens.h"     // loadSceneSprites
#include "systems/screen.h"           // loadTileSheet
#include "systems/sprite_renderer.h"  // renderSprites

namespace kirpich::systems {

namespace {

// The three launch objects: the vehicle and its two smoke plumes, in slots 0, 1 and 2.
constexpr std::size_t kVehicleSlot = 0;
constexpr std::size_t kSmokeSlotLeft = 1;
constexpr std::size_t kSmokeSlotRight = 2;
constexpr std::size_t kLaunchSlotCount = 3;

// Every screen cell either scene writes lands in the second background map.
constexpr std::uint8_t kEmptyCell = static_cast<std::uint8_t>(CharTile::SPACE);

// Where the shared pad's pieces go (tetris.asm:2736-2747; the addresses convert to these rows and
// columns in docs/contracts/launch-scenes.md §2.2).
constexpr std::size_t kBackdropRow = 14;   // $9DC0
constexpr std::size_t kBackdropCol = 0;
constexpr std::size_t kTowerTopRow = 7;    // $9CEC / $9CED
constexpr std::size_t kRightTowerLeftCol = 12;
constexpr std::size_t kRightTowerRightCol = 13;

// The Buran pad adds a left tower and four cells of launch hardware (:2696-2711).
constexpr std::size_t kLeftTowerLeftCol = 6;   // $9CE6
constexpr std::size_t kLeftTowerRightCol = 7;  // $9CE7

// One cell of the umbilical and crew-tunnel hardware: where it sits, and the tile it holds until the
// shuttle's ignition swings it away.
struct PadFitting {
    std::size_t  row;
    std::size_t  col;
    std::uint8_t tile;
};
constexpr std::array<PadFitting, 4> kBuranPadFittings{{
    { .row = 8, .col = 8, .tile = 0x72 },  // $9D08 - umbilical
    { .row = 8, .col = 9, .tile = 0xC4 },  // $9D09 - umbilical
    { .row = 9, .col = 8, .tile = 0xB7 },  // $9D28 - crew tunnel
    { .row = 9, .col = 9, .tile = 0xB8 },  // $9D29 - crew tunnel
}};

// The frame-timer reloads each step holds for. The two chains disagree at every one of them
// (:2720, :2758, :2778, :2953, :2969, :2986, and the ⅙-second step both climbs use).
constexpr std::uint8_t kPadHoldFrames = 187;      // "A hint over 3 seconds. Sigh"
constexpr std::uint8_t kMaximumHoldFrames = 255;  // "4¼ seconds, maximum possible"
constexpr std::uint8_t kRocketRevealFrames = 160;
constexpr std::uint8_t kRocketIgnitionFrames = 128;
constexpr std::uint8_t kClimbStepFrames = 10;  // "⅙ second"

// The shared hold flicker reloads the second timer to 10; the exhaust-frame tails reload it to 6
// (:3071-3072, :2863-2864, :3045-3046).
constexpr std::uint8_t kFlickerFrames = 10;
constexpr std::uint8_t kExhaustFrameFrames = 6;

// Where each climb lights its exhaust, and where it ends. Both terminals sit *above* the value the
// climb starts from, so the coordinate wraps through zero on the way — see the contract §8.
constexpr std::uint8_t kBuranIgnitionY = 0x58;
constexpr std::uint8_t kBuranTerminalY = 0xD0;
constexpr std::uint8_t kBuranExhaustDrop = 0x20;  // exhaust sits this far below the shuttle
constexpr std::uint8_t kBuranExhaustX = 0x4C;
constexpr std::uint8_t kRocketIgnitionY = 0x6A;
constexpr std::uint8_t kRocketTerminalY = 0xE0;
constexpr std::uint8_t kRocketExhaustDrop = 0x10;
constexpr std::uint8_t kRocketExhaustX = 0x54;

// The congratulations message runs along one row of the second map, one letter every six frames, with
// a fixed tile beneath each (:2874-2913). The cursor is a column: the letter index is the offset from
// the first column, and the run ends when the cursor passes the last one.
constexpr std::size_t  kCongratulationsRow = 4;
constexpr std::uint8_t kCongratulationsFirstCol = 2;
constexpr std::uint8_t kCongratulationsEndCol = 18;
constexpr std::uint8_t kCongratulationsUnderTile = 0xB6;

// Clear the whole second map to spaces (ClearTilemap, :6345-6354, entered at $9FFF with a $400 count,
// which is the whole map). It fills with the space glyph, not with zero.
void clearSecondMap(DisplayState& display) {
    for (auto& row : display.secondMap) {
        row.fill(kEmptyCell);
    }
}

// Stamp a rectangular block into the second map at a chosen top-left cell (LoadTilemap.columnLoop,
// :6415-6431: `b` rows of twenty cells, stepping a full map row between them).
void stampBlock(BackgroundMap& map, std::size_t topRow, std::size_t leftCol, const auto& block) {
    for (std::size_t row = 0; row < block.size(); ++row) {
        for (std::size_t col = 0; col < block[row].size(); ++col) {
            map[topRow + row][leftCol + col] = block[row][col];
        }
    }
}

// Stamp a vertical strip downward from a chosen cell (LoadTilemap9C00Row, :3101-3112). The routine's
// name says "Row", but it steps a whole map row between writes, so what it draws is a column.
void stampColumn(BackgroundMap& map, std::size_t topRow, std::size_t col, const auto& strip) {
    for (std::size_t row = 0; row < strip.size(); ++row) {
        map[topRow + row][col] = strip[row];
    }
}

// The part of the pad both scenes share (InitRocketLaunchGraphics, :2729-2748). The disassembly marks
// its name as provisional, and rightly: the Buran calls it too.
void buildLaunchPad(GameContext& game) {
    loadTileSheet(game.display, TileSheet::MULTIPLAYER_BURAN);  // (:2731-2733)
    clearSecondMap(game.display);
    stampBlock(game.display.secondMap, kBackdropRow, kBackdropCol, kBuranBackdropTilemap);
    stampColumn(game.display.secondMap, kTowerTopRow, kRightTowerLeftCol, kRightTowerLeftSideTilemap);
    stampColumn(game.display.secondMap, kTowerTopRow, kRightTowerRightCol, kRightTowerRightSideTilemap);
}

// The hold animation both chains run while waiting (Call_13FA, :3067-3086). It blinks both smoke
// plumes and re-cues the ignition sound on its own timer.
void flickerExhaust(GameContext& game) {
    if (game.flow.timer2 != 0) {
        return;
    }
    game.flow.timer2 = kFlickerFrames;
    game.audioCues.noise = NoiseSfxId::IGNITION;

    // The status byte is XORed with $80 and its domain is closed to {$00,$80}, so this is exactly a
    // visibility toggle. The loop walks slots 1 and 2 — the `ld l, $20` inside it moves the
    // destination on the first pass and is a no-op on the second (:3075-3083).
    SpriteSlot& left = game.spriteRenderer.slots[kSmokeSlotLeft];
    SpriteSlot& right = game.spriteRenderer.slots[kSmokeSlotRight];
    left.hidden = !left.hidden;
    right.hidden = !right.hidden;

    renderSprites(game, kLaunchSlotCount);  // (:3084-3085)
}

// Reveal both smoke plumes and hold — the step both chains take out of their pad state (:2754-2759,
// :2965-2970).
void revealSmoke(GameContext& game, std::uint8_t holdFrames, GameState next) {
    game.spriteRenderer.slots[kSmokeSlotLeft].hidden = false;
    game.spriteRenderer.slots[kSmokeSlotRight].hidden = false;
    game.flow.timer1 = holdFrames;
    game.flow.gameState = next;
}

// One step of a climb toward the ignition height (:2809-2831, :2995-3017). Raises the vehicle a pixel
// and, on reaching the sentinel exactly, lights the exhaust and hands on. Returns whether the sentinel
// was reached; a caller that gets false runs the flicker instead.
bool climbToIgnition(GameContext& game, std::uint8_t sentinelY, std::uint8_t exhaustDrop,
                     std::uint8_t exhaustX, SpriteId exhaust, GameState next) {
    game.flow.timer1 = kClimbStepFrames;

    SpriteSlot& vehicle = game.spriteRenderer.slots[kVehicleSlot];
    --vehicle.y;

    // Equality, not a threshold: a coordinate stepped past the sentinel never triggers.
    if (vehicle.y != sentinelY) {
        return false;
    }

    SpriteSlot& left = game.spriteRenderer.slots[kSmokeSlotLeft];
    left.hidden = false;
    left.y = static_cast<std::uint8_t>(sentinelY + exhaustDrop);
    left.x = exhaustX;
    left.spriteId = exhaust;
    game.spriteRenderer.slots[kSmokeSlotRight].hidden = true;

    renderSprites(game, kLaunchSlotCount);  // (:2826-2827, :3012-3013)
    game.flow.gameState = next;
    game.audioCues.noise = NoiseSfxId::LIFTOFF;
    return true;
}

// The tail both rising states share (:2859-2871, :3041-3053). On its own timer it alternates the
// exhaust between its two frames, which are a consecutive pair, then redraws.
void animateExhaustFrame(GameContext& game) {
    if (game.flow.timer2 == 0) {
        game.flow.timer2 = kExhaustFrameFrames;
        SpriteSlot& exhaust = game.spriteRenderer.slots[kSmokeSlotLeft];
        exhaust.spriteId =
            static_cast<SpriteId>(static_cast<std::uint8_t>(exhaust.spriteId) ^ 1u);
    }
    renderSprites(game, kLaunchSlotCount);
}

// One step of the flight off the top of the screen (:2842-2857, :3028-3039). The exhaust and the
// vehicle both rise; the vehicle's coordinate wraps through zero before it reaches the terminal.
// Returns whether the terminal was reached.
bool climbToTerminal(GameContext& game, std::uint8_t terminalY) {
    game.flow.timer1 = kClimbStepFrames;
    --game.spriteRenderer.slots[kSmokeSlotLeft].y;
    --game.spriteRenderer.slots[kVehicleSlot].y;
    return game.spriteRenderer.slots[kVehicleSlot].y == terminalY;
}

// Restore the gameplay art and put the first map back on screen — what both chains do on the way out
// (:2922-2928, :3057-3064). The sound-driver reset is the caller's, because only one of them asks.
void leaveLaunchScene(GameContext& game, GameState next) {
    loadTileSheet(game.display, TileSheet::GAMEPLAY);
    clearLineClearsList(game);
    game.display.displayed = DisplayedMap::FIRST;
    game.flow.gameState = next;
}

}  // namespace

// ── The Buran chain ─────────────────────────────────────────────────────────────────────────────────

void initBuran(GameContext& game) {
    buildLaunchPad(game);

    // The Buran pad carries a second tower and the launch hardware between them (:2696-2711).
    stampColumn(game.display.secondMap, kTowerTopRow, kLeftTowerLeftCol, kLeftTowerLeftSideTilemap);
    stampColumn(game.display.secondMap, kTowerTopRow, kLeftTowerRightCol, kLeftTowerRightSideTilemap);
    for (const PadFitting& fitting : kBuranPadFittings) {
        game.display.secondMap[fitting.row][fitting.col] = fitting.tile;
    }

    loadSceneSprites(game.spriteRenderer, buranLaunchSprites());
    renderSprites(game, kLaunchSlotCount);  // (:2716-2717)

    game.display.displayed = DisplayedMap::SECOND;  // (:2718-2719, bit 3)
    game.flow.timer1 = kPadHoldFrames;
    game.flow.gameState = GameState::PREPARE_BURAN_LAUNCH;
    game.audioCues.music = MusicId::ROCKET_LAUNCH;
}

void prepareBuranLaunch(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    revealSmoke(game, kMaximumHoldFrames, GameState::BURAN_IGNITION);
}

void buranIgnition(GameContext& game) {
    if (game.flow.timer1 != 0) {
        flickerExhaust(game);
        return;
    }

    game.flow.gameState = GameState::BURAN_IGNITION_2;
    game.spriteRenderer.slots[kSmokeSlotLeft].spriteId = SpriteId::ROCKET_SMOKE_2;
    game.spriteRenderer.slots[kSmokeSlotRight].spriteId = SpriteId::ROCKET_SMOKE_2;
    game.flow.timer1 = kMaximumHoldFrames;
    fillPlayingFieldAndWipe(game, kEmptyCell);  // (:2780-2781)
}

void buranIgnition2(GameContext& game) {
    if (game.flow.timer1 != 0) {
        flickerExhaust(game);
        return;
    }

    game.flow.gameState = GameState::BURAN_LIFTOFF;

    // The umbilicals and crew tunnel swing away (:2794-2802). Only those four cells change.
    for (const PadFitting& fitting : kBuranPadFittings) {
        game.display.secondMap[fitting.row][fitting.col] = kEmptyCell;
    }
}

void buranLiftoff(GameContext& game) {
    if (game.flow.timer1 != 0) {
        flickerExhaust(game);
        return;
    }
    if (!climbToIgnition(game, kBuranIgnitionY, kBuranExhaustDrop, kBuranExhaustX,
                         SpriteId::BURAN_EXHAUST_1, GameState::BURAN_RISING)) {
        flickerExhaust(game);
    }
}

void buranRising(GameContext& game) {
    if (game.flow.timer1 != 0 || !climbToTerminal(game, kBuranTerminalY)) {
        animateExhaustFrame(game);
        return;
    }

    // Seed the print cursor for the congratulations screen (:2851-2854). The port carries the column;
    // the map is already known from `displayed`.
    game.flow.congratulationsColumn = kCongratulationsFirstCol;
    game.flow.gameState = GameState::PRINT_CONGRATULATIONS;
}

void printCongratulations(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    game.flow.timer1 = kExhaustFrameFrames;

    // The cursor is seeded by the rising state and stepped only here, so it is always inside the
    // message. The original indexes its table with no check at all — an unseeded cursor there reads a
    // neighbouring ROM byte, which is meaningless but harmless. A port has no neighbouring byte to
    // read, so the same unreachable case has to be spelled out rather than left to run off the table.
    if (game.flow.congratulationsColumn < kCongratulationsFirstCol ||
        game.flow.congratulationsColumn >= kCongratulationsEndCol) {
        return;
    }

    const std::size_t index = game.flow.congratulationsColumn - kCongratulationsFirstCol;
    const std::size_t col = game.flow.congratulationsColumn;
    game.display.secondMap[kCongratulationsRow][col] = kCongratulationsTilemap[index];
    game.display.secondMap[kCongratulationsRow + 1][col] = kCongratulationsUnderTile;  // (:2895-2898)

    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
    ++game.flow.congratulationsColumn;

    if (game.flow.congratulationsColumn != kCongratulationsEndCol) {
        return;
    }
    game.flow.timer1 = kMaximumHoldFrames;
    game.flow.gameState = GameState::CONGRATULATIONS;
}

void congratulations(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    // No sound-driver reset here; the rocket chain's exit asks for one and this does not (:2918-2929).
    leaveLaunchScene(game, GameState::TYPE_B_VICTORY_JINGLE);
}

// ── The rocket chain ────────────────────────────────────────────────────────────────────────────────

void gameOverToBonusEnding(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    game.flow.gameState = GameState::INIT_ROCKET_LAUNCH;
}

void initRocketLaunch(GameContext& game) {
    // The shared pad and nothing more: no left tower, no umbilicals (:2939-2944).
    buildLaunchPad(game);

    loadSceneSprites(game.spriteRenderer, rocketLaunchSprites());

    // The score earned one of three rockets; the game-over chain recorded which (:2945-2946), and this
    // consumes the record (:2949-2950).
    game.spriteRenderer.slots[kVehicleSlot].spriteId = game.flow.rocketSpriteIndex;
    renderSprites(game, kLaunchSlotCount);  // (:2947-2948)
    game.flow.rocketSpriteIndex = SpriteId{};

    game.display.displayed = DisplayedMap::SECOND;
    game.flow.timer1 = kPadHoldFrames;
    game.flow.gameState = GameState::ROCKET;
    game.audioCues.music = MusicId::ROCKET_LAUNCH;
}

void rocket(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    revealSmoke(game, kRocketRevealFrames, GameState::ROCKET_IGNITION);
}

void rocketIgnition(GameContext& game) {
    if (game.flow.timer1 != 0) {
        flickerExhaust(game);
        return;
    }

    // No smoke art is set here, where the Buran's ignition swaps both plumes (:2982-2988).
    game.flow.gameState = GameState::ROCKET_LIFTOFF;
    game.flow.timer1 = kRocketIgnitionFrames;
    fillPlayingFieldAndWipe(game, kEmptyCell);
}

void rocketLiftoff(GameContext& game) {
    if (game.flow.timer1 != 0) {
        flickerExhaust(game);
        return;
    }
    if (!climbToIgnition(game, kRocketIgnitionY, kRocketExhaustDrop, kRocketExhaustX,
                         SpriteId::ROCKET_EXHAUST_1, GameState::ROCKET_MAIN_ENGINE_FIRE)) {
        flickerExhaust(game);
    }
}

void rocketMainEngineFire(GameContext& game) {
    if (game.flow.timer1 != 0 || !climbToTerminal(game, kRocketTerminalY)) {
        animateExhaustFrame(game);
        return;
    }
    // No cursor is seeded: the rocket chain has no congratulations screen (:3037-3039).
    game.flow.gameState = GameState::END_OF_BONUS_SCENE;
}

void endOfBonusScene(GameContext& game) {
    // No timer gate, unlike every other handler in either chain (:3056-3065).
    game.audioCues.resetRequested = true;  // (:3059)
    leaveLaunchScene(game, GameState::INIT_TYPE_A_DIFFICULTY);
}

// ── Installer ───────────────────────────────────────────────────────────────────────────────────────

void installLaunchSceneHandlers(GameStateDispatcher& dispatcher) {
    dispatcher.setHandler(GameState::INIT_BURAN, initBuran);
    dispatcher.setHandler(GameState::PREPARE_BURAN_LAUNCH, prepareBuranLaunch);
    dispatcher.setHandler(GameState::BURAN_IGNITION, buranIgnition);
    dispatcher.setHandler(GameState::BURAN_IGNITION_2, buranIgnition2);
    dispatcher.setHandler(GameState::BURAN_LIFTOFF, buranLiftoff);
    dispatcher.setHandler(GameState::BURAN_RISING, buranRising);
    dispatcher.setHandler(GameState::PRINT_CONGRATULATIONS, printCongratulations);
    dispatcher.setHandler(GameState::CONGRATULATIONS, congratulations);

    dispatcher.setHandler(GameState::GAME_OVER_TO_BONUS, gameOverToBonusEnding);
    dispatcher.setHandler(GameState::INIT_ROCKET_LAUNCH, initRocketLaunch);
    dispatcher.setHandler(GameState::ROCKET, rocket);
    dispatcher.setHandler(GameState::ROCKET_IGNITION, rocketIgnition);
    dispatcher.setHandler(GameState::ROCKET_LIFTOFF, rocketLiftoff);
    dispatcher.setHandler(GameState::ROCKET_MAIN_ENGINE_FIRE, rocketMainEngineFire);
    dispatcher.setHandler(GameState::END_OF_BONUS_SCENE, endOfBonusScene);
}

}  // namespace kirpich::systems
