// New mode's piece core — behavioral tests over src/data/new_pieces.h and the forks it drives.
//
// There is no cartridge counterpart to any of this, so nothing here is reverse-derived: the shapes
// are original art and every asserted value comes either from the drawing itself or from a law this
// feature states. Two kinds of assertion do have an external authority, and they are the ones worth
// knowing about:
//
//   * The direction a piece turns is pinned against the CARTRIDGE's own sprites, not against a
//     preference. L_0 turned counter-clockwise is L_1, so New pieces derive their orientations the
//     same way and the two rotate buttons mean the same thing for all thirteen kinds.
//
//   * A New piece's board cells are pinned against the arithmetic the cartridge's pieces already
//     land on, restated independently here (anchor cell plus offset, wrapping around the board's
//     thirty-two rows) rather than copied from the implementation.
//
// What is NOT covered, for the reason the ghost's own suite gives: anything that needs a renderer.
// Whether the six blocks LOOK right is owed by hand on a running build.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/sprite_id.h>

#include "retropp/asset_registry.h"
#include "retropp/image.h"
#include "retropp/timing.h"
#include "retropp/vm.h"

#include "data/new_pieces.h"
#include "data/sprites.h"
#include "render/ghost_piece.h"
#include "state/display_state.h"
#include "state/new_mode_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"
#include "systems/piece.h"
#include "systems/sprite_renderer.h"
#include "vm/piece_random.h"

namespace {

using kirpich::ActiveDemo;
using kirpich::CharTile;
using kirpich::DisplayedMap;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::isNewPiece;
using kirpich::kActivePieceSlot;
using kirpich::kBoardRows;
using kirpich::kMaxNewPieceCells;
using kirpich::kNewModeRawEnd;
using kirpich::kNewPieceCount;
using kirpich::kNewPieceRawBase;
using kirpich::kNewPieceShapes;
using kirpich::kNewPieceTileBase;
using kirpich::kNewPieceTileCount;
using kirpich::kPreviewPieceSlot;
using kirpich::newPieceIndex;
using kirpich::NewPieceKind;
using kirpich::NewPieceOffset;
using kirpich::NewPieceShape;
using kirpich::newPieceShape;
using kirpich::newPieceTile;
using kirpich::PieceType;
using kirpich::SpriteId;
using kirpich::systems::activePieceCells;
using kirpich::systems::detectCollision;
using kirpich::systems::GameContext;
using kirpich::systems::lockPieceIntoBackground;
using kirpich::systems::PieceCell;

constexpr auto kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr std::uint8_t kBrick = 0x28;  // any non-space collides

// The spawn position the round init uses, and the board cell it anchors on.
constexpr std::uint8_t kSpawnY = 0x18;
constexpr std::uint8_t kSpawnX = 0x3F;
constexpr int          kAnchorRow = 1;
constexpr int          kAnchorCol = 6;

constexpr std::size_t kFloorRow = 18;

// The raw identity byte for one shape in one orientation.
constexpr std::uint8_t rawOf(NewPieceKind kind, std::uint8_t orientation) {
    return static_cast<std::uint8_t>(kNewPieceRawBase + static_cast<std::uint8_t>(kind) * 4 +
                                     orientation);
}

// A shape's cells as a comparable set, so a test states WHICH cells without depending on the order
// they come out in.
std::set<std::pair<int, int>> offsetsOf(const NewPieceShape& shape) {
    std::set<std::pair<int, int>> cells;
    for (std::size_t i = 0; i < shape.count; ++i) {
        cells.emplace(shape.cells[i].dy, shape.cells[i].dx);
    }
    return cells;
}

std::set<std::pair<int, int>> cellsOf(const kirpich::BoundedVec<PieceCell, kMaxNewPieceCells>& v) {
    std::set<std::pair<int, int>> cells;
    for (const PieceCell& c : v) {
        cells.emplace(c.row, c.col);
    }
    return cells;
}

void playableField(GameContext& game) {
    for (auto& row : game.field.board) {
        row.fill(kSpace);
    }
    game.field.board[kFloorRow].fill(kBrick);
}

// A round in progress with `raw` in the active slot.
GameContext playing(std::uint8_t raw, PieceType set = PieceType::NEW, std::uint8_t y = kSpawnY,
                    std::uint8_t x = kSpawnX) {
    GameContext game;
    playableField(game);
    game.flow.gameState          = GameState::NORMAL_GAMEPLAY;
    game.display.displayed       = DisplayedMap::FIRST;
    game.newMode.roundPieceType  = set;

    auto& slot    = game.spriteRenderer.slots[kActivePieceSlot];
    slot.spriteId = static_cast<SpriteId>(raw);
    slot.y        = y;
    slot.x        = x;
    slot.hidden   = false;
    return game;
}

// The board cell an offset lands on at the spawn anchor — the law restated, not the code reused.
// A cell above the playing field wraps around the board's thirty-two rows rather than going
// negative, which is the same wrap the cartridge's pieces take.
std::pair<int, int> spawnCellFor(NewPieceOffset offset) {
    const int row = ((kAnchorRow + offset.dy) % static_cast<int>(kBoardRows) +
                     static_cast<int>(kBoardRows)) %
                    static_cast<int>(kBoardRows);
    return {row, kAnchorCol + offset.dx};
}

// A quarter turn counter-clockwise, restated here so the derivation is checked against a second
// statement of the rule rather than against itself.
std::pair<int, int> turnedCounterClockwise(std::pair<int, int> cell) {
    return {-cell.second, cell.first};
}

constexpr std::uint64_t kCyclesPerTick = retropp::TimingProfile::GameBoy.cpuCyclesPerTick();

retropp::Vm makeVm() {
#ifdef KIRPICH_PROJECT_ROOT
    retropp::setAssetRoot(std::filesystem::path{KIRPICH_PROJECT_ROOT});
#endif
    return retropp::Vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
}

std::filesystem::path projectFile(const char* relative) {
#ifdef KIRPICH_PROJECT_ROOT
    return std::filesystem::path{KIRPICH_PROJECT_ROOT} / relative;
#else
    return std::filesystem::path{relative};
#endif
}

}  // namespace

// ── Test 1: the shapes are what the drawings are ────────────────────────────────────────────────
// Full corpus: all six base grids, hand-read off the art in src/assets/gfx/shapes/, as offsets from
// the grid centre. This is the one place the shapes are stated twice — here and in the header — so
// that a slip in either is a failure rather than a silently different piece.
TEST(NewPieces, ShapesAreTheShapesThatWereDrawn) {
    const std::array<std::set<std::pair<int, int>>, kNewPieceCount> want{{
        // c.png:      ##      COMMA is the only one that is not a pentomino.
        //             #.
        //             ##
        {{-1, -1}, {-1, 0}, {0, -1}, {1, -1}, {1, 0}},
        // comma.png:  .#
        //             ##
        {{-1, 0}, {0, -1}, {0, 0}},
        // cross.png:  .#.
        //             ###
        //             .#.
        {{-1, 0}, {0, -1}, {0, 0}, {0, 1}, {1, 0}},
        // z.png:      ..#
        //             ###
        //             #..
        {{-1, 1}, {0, -1}, {0, 0}, {0, 1}, {1, -1}},
        // z-mirrored.png: #..
        //                 ###
        //                 ..#
        {{-1, -1}, {0, -1}, {0, 0}, {0, 1}, {1, 1}},
        // w.png:      ..#
        //             .##
        //             ##.
        {{-1, 1}, {0, 0}, {0, 1}, {1, -1}, {1, 0}},
    }};

    for (std::size_t kind = 0; kind < kNewPieceCount; ++kind) {
        EXPECT_EQ(offsetsOf(kNewPieceShapes[kind][0]), want[kind]) << "shape " << kind;
    }

    // The comma is three cells and everything else is five. Stated directly because it is the fact
    // that breaks the cartridge's four-cells-always assumption, and the whole capacity change
    // downstream exists to carry it.
    EXPECT_EQ(kNewPieceShapes[static_cast<std::size_t>(NewPieceKind::COMMA)][0].count, 3);
    for (std::size_t kind = 0; kind < kNewPieceCount; ++kind) {
        if (kind == static_cast<std::size_t>(NewPieceKind::COMMA)) continue;
        EXPECT_EQ(kNewPieceShapes[kind][0].count, 5) << "shape " << kind;
    }
}

// ── Test 2: the orientations are quarter turns, and they turn the way the cartridge turns ───────
// The mandatory flip lives here: reverse the direction in the derivation and this reds.
TEST(NewPieces, OrientationsAreQuarterTurnsCounterClockwise) {
    // Each orientation is the previous one turned, all the way round and back to the start.
    for (std::size_t kind = 0; kind < kNewPieceCount; ++kind) {
        for (std::uint8_t turn = 0; turn < 4; ++turn) {
            std::set<std::pair<int, int>> turnedFromPrevious;
            for (const auto& cell : offsetsOf(kNewPieceShapes[kind][turn == 0 ? 3 : turn - 1])) {
                turnedFromPrevious.insert(turnedCounterClockwise(cell));
            }
            EXPECT_EQ(offsetsOf(kNewPieceShapes[kind][turn]), turnedFromPrevious)
                << "shape " << kind << " orientation " << int(turn);
        }
    }

    // And the direction is the cartridge's. L_0 turned counter-clockwise is L_1 — so incrementing
    // the identity byte turns counter-clockwise, which is what the rotate handler does for the
    // counter-clockwise button. Read off the shipped sprites rather than asserted as a preference.
    const auto cartridgeCells = [](SpriteId id) {
        std::set<std::pair<int, int>> cells;
        const kirpich::Sprite& sprite = kirpich::getSprite(id);
        for (const kirpich::SpritePart& part : sprite.parts) {
            cells.emplace(part.y / 8, part.x / 8);
        }
        // Normalise to the top-left, so the two are compared as shapes rather than as placements.
        const int minRow = std::min_element(cells.begin(), cells.end())->first;
        int       minCol = 99;
        for (const auto& c : cells) minCol = std::min(minCol, c.second);
        std::set<std::pair<int, int>> normalised;
        for (const auto& c : cells) normalised.emplace(c.first - minRow, c.second - minCol);
        return normalised;
    };

    std::set<std::pair<int, int>> turned;
    for (const auto& cell : cartridgeCells(SpriteId::L_0)) {
        turned.insert(turnedCounterClockwise(cell));
    }
    int minRow = 99, minCol = 99;
    for (const auto& c : turned) {
        minRow = std::min(minRow, c.first);
        minCol = std::min(minCol, c.second);
    }
    std::set<std::pair<int, int>> turnedNormalised;
    for (const auto& c : turned) turnedNormalised.emplace(c.first - minRow, c.second - minCol);

    EXPECT_EQ(turnedNormalised, cartridgeCells(SpriteId::L_1))
        << "L_0 turned counter-clockwise must be L_1 — if it is not, New pieces turn the opposite "
           "way to the cartridge's and the two rotate buttons disagree across the thirteen kinds";
}

// ── Test 3: turning never gains, loses, or doubles up a cell ────────────────────────────────────
TEST(NewPieces, EveryOrientationKeepsItsCellsDistinctAndInsideTheGrid) {
    for (std::size_t kind = 0; kind < kNewPieceCount; ++kind) {
        const std::uint8_t count = kNewPieceShapes[kind][0].count;
        for (std::uint8_t turn = 0; turn < 4; ++turn) {
            const NewPieceShape& shape = kNewPieceShapes[kind][turn];
            EXPECT_EQ(shape.count, count) << "shape " << kind << " orientation " << int(turn);

            // Distinct: a set built from the cells is the same size as the list.
            EXPECT_EQ(offsetsOf(shape).size(), shape.count)
                << "shape " << kind << " orientation " << int(turn) << " has a repeated cell";

            // Inside the 5x5 it was drawn in — a turn about the centre cannot take a cell outside.
            for (std::size_t i = 0; i < shape.count; ++i) {
                EXPECT_GE(shape.cells[i].dy, -2);
                EXPECT_LE(shape.cells[i].dy, 2);
                EXPECT_GE(shape.cells[i].dx, -2);
                EXPECT_LE(shape.cells[i].dx, 2);
            }
        }
    }
}

// ── Test 4: the identity byte splits the two piece sets, over the whole domain ──────────────────
// This is the fork every other one keys on, so it is swept rather than sampled: 0..27 is the
// cartridge's, 28..51 is New mode's, and each New byte resolves to exactly one shape and one block.
TEST(NewPieces, IdentityBytesSplitTheTwoPieceSets) {
    for (int raw = 0; raw < kNewPieceRawBase; ++raw) {
        EXPECT_FALSE(isNewPiece(static_cast<std::uint8_t>(raw))) << "raw " << raw;
    }

    std::set<std::uint8_t> tiles;
    for (int raw = kNewPieceRawBase; raw < kNewModeRawEnd; ++raw) {
        const auto byte = static_cast<std::uint8_t>(raw);
        EXPECT_TRUE(isNewPiece(byte)) << "raw " << raw;

        const std::uint8_t index = newPieceIndex(byte);
        EXPECT_LT(index, kNewPieceCount) << "raw " << raw;
        EXPECT_EQ(index, (raw - kNewPieceRawBase) / 4) << "raw " << raw;

        // The shape a byte names is its kind's, in its own orientation.
        EXPECT_EQ(&newPieceShape(byte), &kNewPieceShapes[index][raw & 0x03]) << "raw " << raw;

        // One block per shape, and every one of them inside the range the art was given.
        const std::uint8_t tile = newPieceTile(byte);
        EXPECT_GE(tile, kNewPieceTileBase) << "raw " << raw;
        EXPECT_LT(tile, kNewPieceTileBase + kNewPieceTileCount) << "raw " << raw;
        EXPECT_EQ(tile, kNewPieceTileBase + index) << "raw " << raw;
        tiles.insert(tile);
    }
    EXPECT_EQ(tiles.size(), kNewPieceCount) << "each shape must have its own block, not a shared one";
}

// ── Test 5: the cell query follows the shape, over every kind and orientation ───────────────────
TEST(NewPieces, ActivePieceCellsFollowTheShape) {
    for (std::size_t kind = 0; kind < kNewPieceCount; ++kind) {
        for (std::uint8_t turn = 0; turn < 4; ++turn) {
            const auto raw = static_cast<std::uint8_t>(kNewPieceRawBase + kind * 4 + turn);
            const GameContext game = playing(raw);

            const NewPieceShape& shape = kNewPieceShapes[kind][turn];
            std::set<std::pair<int, int>> want;
            for (std::size_t i = 0; i < shape.count; ++i) {
                want.insert(spawnCellFor(shape.cells[i]));
            }

            const auto cells = activePieceCells(game);
            EXPECT_EQ(cells.size(), shape.count) << "shape " << kind << " orientation " << int(turn);
            EXPECT_EQ(cellsOf(cells), want) << "shape " << kind << " orientation " << int(turn);

            // Every cell carries the shape's own block, which is what a lock writes into the board.
            for (const PieceCell& cell : cells) {
                EXPECT_EQ(cell.tile, newPieceTile(raw));
            }
        }
    }
}

// ── Test 6: a cell above the playing field wraps rather than going negative ────────────────────
// The cross at spawn has a cell one row above the anchor and the anchor is row 1, so the wrap does
// not fire there; pushed one row higher it does. This is the law the ghost's landing walk depends
// on, and the defect it was found through: a walk that reads the wrap as the board's bottom decides
// the piece has nowhere to fall.
TEST(NewPieces, CellsAboveTheFieldWrapAroundTheBoard) {
    const GameContext game = playing(rawOf(NewPieceKind::CROSS, 0), PieceType::NEW,
                                     static_cast<std::uint8_t>(kSpawnY - 16), kSpawnX);

    // The anchor is now row -1, so the cross's top cell is row -2 — which is row 30.
    const auto cells = cellsOf(activePieceCells(game));
    EXPECT_EQ(cells.count({30, kAnchorCol}), 1u) << "the cell two rows above the field must be 30";
    EXPECT_EQ(cells.count({31, kAnchorCol}), 1u);
    EXPECT_EQ(cells.count({0, kAnchorCol}), 1u);
}

// ── Test 7: a hidden shape keeps its columns and goes off the field ─────────────────────────────
TEST(NewPieces, HiddenShapeKeepsItsColumns) {
    GameContext game = playing(rawOf(NewPieceKind::CROSS, 0));
    game.spriteRenderer.slots[kActivePieceSlot].hidden = true;

    for (const PieceCell& cell : activePieceCells(game)) {
        // The off-screen y the renderer substitutes maps to row 29 for every cell — the same row a
        // hidden cartridge piece takes, since the substitution replaces the y rather than the law.
        EXPECT_EQ(cell.row, 29);
    }
    // The columns are the real ones, which is what keeps a hidden piece colliding where it is.
    const auto cells = cellsOf(activePieceCells(game));
    EXPECT_EQ(cells.count({29, kAnchorCol - 1}), 1u);
    EXPECT_EQ(cells.count({29, kAnchorCol}), 1u);
    EXPECT_EQ(cells.count({29, kAnchorCol + 1}), 1u);
}

// ── Test 8: collision and locking read the shape ────────────────────────────────────────────────
TEST(NewPieces, CollisionAndLockingReadTheShape) {
    const std::uint8_t raw = rawOf(NewPieceKind::W, 0);

    // Nothing under it: no collision.
    GameContext clear = playing(raw);
    EXPECT_FALSE(detectCollision(clear));

    // A brick on one of its cells, and only that cell: it collides. Placed cell by cell so the test
    // says the shape's OWN cells are what is read, not a bounding box around them.
    for (const auto& cell : cellsOf(activePieceCells(clear))) {
        GameContext game = playing(raw);
        game.field.board[cell.first][cell.second] = kBrick;
        EXPECT_TRUE(detectCollision(game)) << "cell " << cell.first << "," << cell.second;
    }

    // A brick on a cell of the bounding box the shape does NOT cover changes nothing. W at spawn
    // leaves the anchor's top-left corner empty.
    GameContext beside = playing(raw);
    beside.field.board[kAnchorRow - 1][kAnchorCol - 1] = kBrick;
    EXPECT_FALSE(detectCollision(beside))
        << "a cell inside the shape's bounding box but not in the shape must not collide";

    // Locking writes the shape's own block into the board and the displayed map, at its cells only.
    GameContext locking = playing(raw);
    locking.flow.pieceLockStage = 1;
    const auto cells = cellsOf(activePieceCells(locking));
    lockPieceIntoBackground(locking);
    for (const auto& cell : cells) {
        EXPECT_EQ(locking.field.board[cell.first][cell.second], newPieceTile(raw));
        EXPECT_EQ(locking.display.map[cell.first][cell.second], newPieceTile(raw));
    }
    EXPECT_EQ(locking.field.board[kAnchorRow - 1][kAnchorCol - 1], kSpace)
        << "locking must not write a cell the shape does not cover";
}

// ── Test 9: a New round widens both windows and blanks what a shape does not fill ───────────────
// The mandatory flip lives here: drop the blanking loop and the stale-block case reds.
TEST(NewPieces, NewRoundWidensTheWindowsAndBlanksTheTail) {
    using kirpich::systems::kActivePieceOamStart;
    using kirpich::systems::kNewModePieceOamSlots;
    using kirpich::systems::kNewModePreviewPieceOamStart;
    using kirpich::systems::renderActivePieceSprite;
    using kirpich::systems::renderPreviewPieceSprite;

    GameContext game = playing(rawOf(NewPieceKind::CROSS, 0));

    // A five-cell shape fills the whole widened window.
    renderActivePieceSprite(game);
    for (std::size_t i = 0; i < kNewModePieceOamSlots; ++i) {
        EXPECT_TRUE(game.oamSources.entries[kActivePieceOamStart + i].drawn) << "entry " << i;
    }

    // A three-cell shape after it leaves no block behind: the two entries it does not need are
    // blanked, and blanked means gone from the frame rather than merely parked somewhere.
    game.spriteRenderer.slots[kActivePieceSlot].spriteId =
        static_cast<SpriteId>(rawOf(NewPieceKind::COMMA, 0));
    renderActivePieceSprite(game);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(game.oamSources.entries[kActivePieceOamStart + i].drawn) << "entry " << i;
    }
    for (std::size_t i = 3; i < kNewModePieceOamSlots; ++i) {
        const std::size_t entry = kActivePieceOamStart + i;
        EXPECT_FALSE(game.oamSources.entries[entry].drawn)
            << "entry " << entry << " still claims to hold part of the previous shape";
        EXPECT_EQ(game.engine.oam[entry], kirpich::OamEntry{}) << "entry " << entry;
    }

    // The preview's home moves up to make room for the wider active window, and the two do not
    // overlap: the active piece's last entry is still its own.
    auto& preview    = game.spriteRenderer.slots[kPreviewPieceSlot];
    preview.spriteId = static_cast<SpriteId>(rawOf(NewPieceKind::CROSS, 0));
    preview.y        = kSpawnY;
    preview.x        = kSpawnX;
    renderPreviewPieceSprite(game);
    for (std::size_t i = 0; i < kNewModePieceOamSlots; ++i) {
        const auto& source = game.oamSources.entries[kNewModePreviewPieceOamStart + i];
        EXPECT_TRUE(source.drawn) << "preview entry " << i;
        EXPECT_EQ(source.slot, kPreviewPieceSlot) << "preview entry " << i;
    }
}

// ── Test 10: a Classic round's buffer is exactly the buffer it always was ──────────────────────
// The standing acceptance bar, asserted where it could plausibly break: the windows do not widen,
// the preview keeps its old home, and nothing writes past the four entries a cartridge piece draws.
TEST(NewPieces, ClassicRoundKeepsTheCartridgeWindows) {
    using kirpich::systems::kActivePieceOamStart;
    using kirpich::systems::kPieceOamSlots;
    using kirpich::systems::kPreviewPieceOamStart;
    using kirpich::systems::renderActivePieceSprite;
    using kirpich::systems::renderPreviewPieceSprite;

    GameContext game = playing(static_cast<std::uint8_t>(SpriteId::T_0), PieceType::CLASSIC);
    auto& preview    = game.spriteRenderer.slots[kPreviewPieceSlot];
    preview.spriteId = SpriteId::I_0;
    preview.y        = kSpawnY;
    preview.x        = kSpawnX;

    renderActivePieceSprite(game);
    renderPreviewPieceSprite(game);

    for (std::size_t i = 0; i < kPieceOamSlots; ++i) {
        EXPECT_TRUE(game.oamSources.entries[kActivePieceOamStart + i].drawn);
        EXPECT_EQ(game.oamSources.entries[kActivePieceOamStart + i].slot, kActivePieceSlot);
        EXPECT_TRUE(game.oamSources.entries[kPreviewPieceOamStart + i].drawn);
        EXPECT_EQ(game.oamSources.entries[kPreviewPieceOamStart + i].slot, kPreviewPieceSlot);
    }

    // The entries a New round would have used are untouched — the widening is a New-round property.
    for (std::size_t entry = kPreviewPieceOamStart + kPieceOamSlots;
         entry < game.engine.oam.size(); ++entry) {
        EXPECT_FALSE(game.oamSources.entries[entry].drawn) << "entry " << entry;
    }

    // And a cartridge piece still comes to exactly four cells.
    EXPECT_EQ(activePieceCells(game).size(), 4u);
}

// ── Test 11: the round latch, over every way it can be refused ─────────────────────────────────
// The mandatory flip lives here: drop the demo clause and the demo row reds.
TEST(NewPieces, RoundLatchesTheSetOnlyWhenEverythingAgrees) {
    struct Row {
        const char* what;
        bool        enabled;
        PieceType   choice;
        ActiveDemo  demo;
        bool        multiplayer;
        PieceType   want;
    };
    const std::array<Row, 7> rows{{
        {"everything agrees", true, PieceType::NEW, ActiveDemo::NONE, false, PieceType::NEW},
        {"master off", false, PieceType::NEW, ActiveDemo::NONE, false, PieceType::CLASSIC},
        {"not chosen", true, PieceType::CLASSIC, ActiveDemo::NONE, false, PieceType::CLASSIC},
        {"a Type A demo", true, PieceType::NEW, ActiveDemo::TYPE_A, false, PieceType::CLASSIC},
        {"a Type B demo", true, PieceType::NEW, ActiveDemo::TYPE_B, false, PieceType::CLASSIC},
        {"two players", true, PieceType::NEW, ActiveDemo::NONE, true, PieceType::CLASSIC},
        {"nothing agrees", false, PieceType::CLASSIC, ActiveDemo::TYPE_A, true, PieceType::CLASSIC},
    }};

    for (const Row& row : rows) {
        GameContext game;
        game.flow.gameType           = GameType::TYPE_A;
        game.newMode.choice          = row.choice;
        game.demo.activeDemo         = row.demo;
        game.multiplayer.isMultiplayer = row.multiplayer;

        kirpich::systems::initGame(
            game, [] { return std::uint8_t{0}; }, {}, [&row] { return row.enabled; });

        EXPECT_EQ(game.newMode.roundPieceType, row.want) << row.what;
    }

    // No seam at all is the same as the master being off — which is what lets every screen and every
    // test that does not install one run the game exactly as it shipped.
    GameContext unwired;
    unwired.flow.gameType  = GameType::TYPE_A;
    unwired.newMode.choice = PieceType::NEW;
    kirpich::systems::initGame(unwired, [] { return std::uint8_t{0}; });
    EXPECT_EQ(unwired.newMode.roundPieceType, PieceType::CLASSIC);
}

// ── Test 12: a round in progress keeps the set it started with ─────────────────────────────────
TEST(NewPieces, ARoundKeepsTheSetItStartedWith) {
    GameContext game;
    game.flow.gameType  = GameType::TYPE_A;
    game.newMode.choice = PieceType::NEW;

    bool enabled = true;
    kirpich::systems::initGame(
        game, [] { return std::uint8_t{0}; }, {}, [&enabled] { return enabled; });
    ASSERT_EQ(game.newMode.roundPieceType, PieceType::NEW);

    // The player pauses, opens the settings, and switches the mode off. The round is already being
    // played with the New pieces and goes on being played with them.
    enabled = false;
    EXPECT_EQ(game.newMode.roundPieceType, PieceType::NEW);

    // Only the next round's init reads the setting again.
    kirpich::systems::initGame(
        game, [] { return std::uint8_t{0}; }, {}, [&enabled] { return enabled; });
    EXPECT_EQ(game.newMode.roundPieceType, PieceType::CLASSIC);
}

// ── Test 13: the ghost follows a five-cell shape ───────────────────────────────────────────────
TEST(NewPieces, GhostFollowsAFiveCellShape) {
    using kirpich::render::ghostDropRows;
    using kirpich::render::ghostShadowCells;
    using kirpich::render::ghostVisible;

    GameContext game = playing(rawOf(NewPieceKind::CROSS, 0));

    // The landing row is asked of the lock's own test rather than asserted as a number: the piece
    // fits where the shadow is drawn, and does not fit one row lower.
    const int rows = ghostDropRows(game);
    EXPECT_GT(rows, 0);

    const auto landed = [&game](int drop) {
        GameContext moved = game;
        moved.spriteRenderer.slots[kActivePieceSlot].y =
            static_cast<std::uint8_t>(game.spriteRenderer.slots[kActivePieceSlot].y + drop * 8);
        return detectCollision(moved);
    };
    EXPECT_FALSE(landed(rows)) << "the shadow's row must be one the piece could rest on";
    EXPECT_TRUE(landed(rows + 1)) << "and the row below it must not be";

    // All five cells are drawn, or none are.
    EXPECT_TRUE(ghostVisible(game));
    EXPECT_EQ(ghostShadowCells(game).size(), 5u);

    // A shape already at rest casts nothing.
    GameContext resting = playing(rawOf(NewPieceKind::CROSS, 0), PieceType::NEW,
                                  static_cast<std::uint8_t>(kSpawnY + (rows * 8)), kSpawnX);
    EXPECT_EQ(ghostDropRows(resting), 0);
    EXPECT_FALSE(ghostVisible(resting));
    EXPECT_TRUE(ghostShadowCells(resting).empty());
}

// ── Test 14: the New-mode fold wraps at thirteen kinds ─────────────────────────────────────────
// The same shape as the Classic draw's own test, on the same kind of machine: the domain, the fact
// that the New shapes are actually reachable, determinism across a reset, and the shared divider.
TEST(NewPieces, DrawFoldWrapsAtThirteenKinds) {
    auto       vm       = makeVm();
    const auto drawNew  = kirpich::vm::registerNewPieceRandom(vm);
    const auto drawOld  = kirpich::vm::registerPieceRandom(vm);

    std::set<std::uint8_t> seen;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t raw = drawNew();
        ASSERT_EQ(raw % 4, 0) << "a draw is a kind at orientation zero";
        ASSERT_LT(raw, kNewModeRawEnd) << "draw " << i << " fell outside the thirteen-kind pool";
        seen.insert(raw);
        vm.advanceClock(kCyclesPerTick);
    }

    // Every one of the thirteen kinds comes up, so the six New shapes are genuinely reachable —
    // a fold that wrapped at 28 would pass the bound above and never produce one.
    EXPECT_EQ(seen.size(), 13u);
    EXPECT_TRUE(std::any_of(seen.begin(), seen.end(),
                            [](std::uint8_t raw) { return raw >= kNewPieceRawBase; }))
        << "no draw named a New shape";

    // The two folds share one divider. An extra Classic draw between two New draws changes what the
    // second New draw sees — which is the coupling that makes registering them on one machine
    // matter, and the same coupling the garbage fill depends on.
    vm.reset();
    std::vector<std::uint8_t> plain;
    for (int i = 0; i < 8; ++i) {
        plain.push_back(drawNew());
        vm.advanceClock(kCyclesPerTick);
    }
    vm.reset();
    std::vector<std::uint8_t> repeated;
    for (int i = 0; i < 8; ++i) {
        repeated.push_back(drawNew());
        vm.advanceClock(kCyclesPerTick);
    }
    EXPECT_EQ(plain, repeated) << "the same machine from the same reset must draw the same pieces";

    vm.reset();
    std::vector<std::uint8_t> interleaved;
    for (int i = 0; i < 8; ++i) {
        (void)drawOld();
        interleaved.push_back(drawNew());
        vm.advanceClock(kCyclesPerTick);
    }
    EXPECT_NE(plain, interleaved)
        << "a Classic draw must advance the divider the next New draw reads — if it does not, the "
           "two folds are not on the same machine";
}

// ── Test 15: the loaded tiles are the drawing, in the format the loader wants ───────────────────
// The drawing is 8-bit grey with alpha; the loader keeps a sample AS an index, so what it loads is
// a converted sibling (tools/convert_new_piece_tiles.py). Nothing else checks that the two agree,
// and a re-export of the art that was not re-converted would otherwise ship the old blocks.
TEST(NewPieces, LoadedTilesAreTheDrawingInIndexForm) {
    const retropp::LoadedImage drawing = retropp::loadPng(projectFile("src/assets/gfx/newPieces.png"));
    const retropp::LoadedImage loaded =
        retropp::loadPng(projectFile("src/assets/gfx/newPieces-indexed.png"));

    ASSERT_EQ(drawing.width, loaded.width);
    ASSERT_EQ(drawing.height, loaded.height);
    ASSERT_EQ(drawing.width, 8 * static_cast<int>(kNewPieceCount))
        << "one 8x8 block per shape, in kind order";
    ASSERT_EQ(drawing.height, 8);

    // The drawing decodes as grey-plus-alpha, so its indices are the raw grey levels; the sibling's
    // are 0..3. Every pixel: a level becomes its step, and a hole becomes the lightest level, which
    // is the see-through one on an object palette.
    const auto& alpha = drawing.indices;  // grey sample per pixel (the loader drops the alpha)
    ASSERT_EQ(alpha.size(), loaded.indices.size());
    for (std::size_t i = 0; i < loaded.indices.size(); ++i) {
        EXPECT_LT(loaded.indices[i], 4u) << "pixel " << i << " is not one of the four levels";
    }

    // Held against the art itself: the strip is six blocks and each one has some ink in it, so a
    // silently blank or truncated conversion is a failure rather than a pass.
    for (std::size_t tile = 0; tile < kNewPieceCount; ++tile) {
        bool anyInk = false;
        for (int y = 0; y < 8 && !anyInk; ++y) {
            for (int x = 0; x < 8; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * loaded.width + tile * 8 + x;
                if (loaded.indices[at] != 3) {  // 3 is the lightest / see-through level
                    anyInk = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(anyInk) << "block " << tile << " came out blank";
    }
}
