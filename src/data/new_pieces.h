#pragma once

// The New-mode pieces: six shapes the cartridge never had.
//
// These are original work, not something read out of a ROM — so unlike every other picture and table
// in the game they are committed, they ship, and they never go through the extractor. There is no
// disassembly to be faithful to here and nothing to reverse-derive; the shapes are simply what they
// are drawn as, and this file is the drawing written down.
//
// Each shape is authored as a 5x5 grid of characters, its bounding box centred (ties toward the
// top-left), so a reader can hold the literal up against src/assets/gfx/shapes/<name>.png and see
// the same picture. Nothing else about a shape is hand-written: the four orientations are derived by
// rotating the grid about its centre, so there are no rotation tables to get out of step with each
// other, and a cell list is whatever the grid says it is — three cells for the comma, five for the
// rest. The four-cell assumption the cartridge's pieces are built on does not carry over.
//
// Identity continues the cartridge's own packed byte (kirpich::Piece — kind * 4 + rotation). The
// seven cartridge kinds are 0..6, so these are 7..12 and their raw bytes run 28..51. That numbering
// is what lets every fork in the piece system ask about the byte rather than about a mode flag:
// Classic's draw folds at 28 and cannot produce one of these, so a Classic round takes the
// cartridge path by construction rather than by a check someone has to remember to write.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kirpich {

// How many shapes New mode adds. The settings sub-screen quotes this in its description, so the
// count is derived from the roster rather than written out in prose twice.
inline constexpr std::size_t kNewPieceCount = 6;

// Where these kinds start, in the packed identity byte's terms. The cartridge has seven kinds.
inline constexpr std::uint8_t kCartridgePieceKindCount = 7;
inline constexpr std::uint8_t kNewPieceRawBase =
    static_cast<std::uint8_t>(kCartridgePieceKindCount * 4);  // 28

// The whole pool a New round draws from: the cartridge's seven plus these six.
inline constexpr std::uint8_t kNewModeKindCount =
    static_cast<std::uint8_t>(kCartridgePieceKindCount + kNewPieceCount);  // 13
inline constexpr std::uint8_t kNewModeRawEnd = static_cast<std::uint8_t>(kNewModeKindCount * 4);  // 52

static_assert(kNewPieceRawBase == 28, "the cartridge's seven kinds occupy raw bytes 0..27");
static_assert(kNewModeRawEnd == 52, "the New-mode fold wraps at 13 kinds x 4 orientations");

// Which shape, in the order they are drawn in the tile strip.
enum class NewPieceKind : std::uint8_t {
    C          = 0,
    COMMA      = 1,
    CROSS      = 2,
    Z          = 3,
    Z_MIRRORED = 4,
    W          = 5,
};

// The authoring grid, and the cell every offset is measured from.
inline constexpr std::size_t kNewPieceGridSize   = 5;
inline constexpr std::size_t kNewPieceGridCentre = kNewPieceGridSize / 2;  // (2, 2)

// No shape may cover more cells than this. Five is the widest pentomino here; the piece system's
// cell query and the ghost both carry this capacity.
inline constexpr std::size_t kMaxNewPieceCells = 5;

// Where these shapes' block art sits in the tile-index space.
//
// The gameplay tile block holds 197 real tiles from index $30, so everything from $F5 up is index
// space no art was ever loaded into — the loader copies more than the sheet holds and the surplus is
// whatever followed it in the cartridge, which nothing draws (src/render/tile_atlas.h). Six of those
// free indices name these six blocks instead. A locked New piece therefore writes an ordinary board
// byte and reaches the screen through the ordinary tile pipeline, with nothing about the board,
// the wipe, the flash or the ghost needing to know it is unusual.
inline constexpr std::uint8_t  kNewPieceTileBase  = 0xF5;
inline constexpr std::uint16_t kNewPieceTileCount = static_cast<std::uint16_t>(kNewPieceCount);

static_assert(kNewPieceTileBase + kNewPieceTileCount - 1 == 0xFA,
              "the six block tiles occupy $F5..$FA");

// One cell of a shape, as a signed offset in cells from the grid's centre.
struct NewPieceOffset {
    std::int8_t dy = 0;
    std::int8_t dx = 0;

    friend constexpr bool operator==(const NewPieceOffset&, const NewPieceOffset&) = default;
};

// One shape in one orientation: its cells, in the base grid's reading order.
//
// A plain array and a count rather than a BoundedVec, because the list is built a cell at a time as
// the grid is walked and BoundedVec has no mutating operations by design.
struct NewPieceShape {
    std::array<NewPieceOffset, kMaxNewPieceCells> cells{};
    std::uint8_t                                  count = 0;

    friend constexpr bool operator==(const NewPieceShape&, const NewPieceShape&) = default;
};

// The six shapes as drawn. Row 0 is the top; '#' is a cell, anything else is empty.
using NewPieceGrid = std::array<std::string_view, kNewPieceGridSize>;

inline constexpr std::array<NewPieceGrid, kNewPieceCount> kNewPieceGrids{{
    // C — src/assets/gfx/shapes/c.png
    NewPieceGrid{".....",
                 ".##..",
                 ".#...",
                 ".##..",
                 "....."},
    // COMMA — src/assets/gfx/shapes/comma.png. Three cells; the only one that is not a pentomino.
    NewPieceGrid{".....",
                 "..#..",
                 ".##..",
                 ".....",
                 "....."},
    // CROSS — src/assets/gfx/shapes/cross.png
    NewPieceGrid{".....",
                 "..#..",
                 ".###.",
                 "..#..",
                 "....."},
    // Z — src/assets/gfx/shapes/z.png
    NewPieceGrid{".....",
                 "...#.",
                 ".###.",
                 ".#...",
                 "....."},
    // Z_MIRRORED — src/assets/gfx/shapes/z-mirrored.png. A genuinely distinct shape: this pentomino
    // is chiral, so its mirror is not any rotation of it — the same reason the cartridge carries both
    // S and Z.
    NewPieceGrid{".....",
                 ".#...",
                 ".###.",
                 "...#.",
                 "....."},
    // W — src/assets/gfx/shapes/w.png
    NewPieceGrid{".....",
                 "...#.",
                 "..##.",
                 ".##..",
                 "....."},
}};

// A quarter turn counter-clockwise, in the screen's axes: y counts down, x counts right, so the cell
// above the centre becomes the cell to its left.
//
// Counter-clockwise is the direction the identity byte's orientation counts in. The cartridge's own
// sprites are laid out that way — L_1 is L_0 turned counter-clockwise — and the rotate handler
// increments the byte for the counter-clockwise button and decrements it for the clockwise one, so
// deriving these turns in the same direction is what makes the two buttons agree with the seven
// pieces a player already knows.
[[nodiscard]] constexpr NewPieceOffset rotatedCounterClockwise(NewPieceOffset offset) {
    return NewPieceOffset{.dy = static_cast<std::int8_t>(-offset.dx),
                          .dx = static_cast<std::int8_t>(offset.dy)};
}

// One grid, turned `turns` quarter turns counter-clockwise, as a cell list.
//
// Cells come out in the BASE grid's reading order whatever the orientation, so a given block of the
// piece keeps its place in the list as the piece turns.
[[nodiscard]] constexpr NewPieceShape newPieceShapeFromGrid(const NewPieceGrid& grid, int turns) {
    NewPieceShape shape;
    for (std::size_t row = 0; row < kNewPieceGridSize; ++row) {
        for (std::size_t col = 0; col < kNewPieceGridSize; ++col) {
            if (grid[row][col] != '#') {
                continue;
            }
            NewPieceOffset offset{
                .dy = static_cast<std::int8_t>(static_cast<int>(row) -
                                               static_cast<int>(kNewPieceGridCentre)),
                .dx = static_cast<std::int8_t>(static_cast<int>(col) -
                                               static_cast<int>(kNewPieceGridCentre)),
            };
            for (int turn = 0; turn < turns; ++turn) {
                offset = rotatedCounterClockwise(offset);
            }
            // Past capacity this indexes out of the array, which is not a constant expression — so a
            // shape drawn with too many cells fails to compile rather than silently losing one.
            shape.cells[shape.count++] = offset;
        }
    }
    return shape;
}

// How many orientations a piece has, which is the range of the identity byte's low two bits.
inline constexpr std::size_t kPieceOrientations = 4;

// Every shape in every orientation, derived once at compile time.
inline constexpr std::array<std::array<NewPieceShape, kPieceOrientations>, kNewPieceCount>
    kNewPieceShapes = [] {
        std::array<std::array<NewPieceShape, kPieceOrientations>, kNewPieceCount> all{};
        for (std::size_t kind = 0; kind < kNewPieceCount; ++kind) {
            for (std::size_t turns = 0; turns < kPieceOrientations; ++turns) {
                all[kind][turns] = newPieceShapeFromGrid(kNewPieceGrids[kind], static_cast<int>(turns));
            }
        }
        return all;
    }();

// Turning a shape moves its cells; it never gains or loses one.
static_assert([] {
    for (const auto& orientations : kNewPieceShapes) {
        for (const NewPieceShape& shape : orientations) {
            if (shape.count == 0 || shape.count != orientations[0].count) {
                return false;
            }
        }
    }
    return true;
}(), "every orientation of a shape must cover the same, non-zero, number of cells");

// Whether an identity byte names one of these shapes rather than a cartridge piece. This is the
// question every fork in the piece system asks.
[[nodiscard]] constexpr bool isNewPiece(std::uint8_t raw) { return raw >= kNewPieceRawBase; }

// Which of the six a raw identity byte names. Only meaningful when isNewPiece(raw).
[[nodiscard]] constexpr std::uint8_t newPieceIndex(std::uint8_t raw) {
    return static_cast<std::uint8_t>((raw >> 2) - kCartridgePieceKindCount);
}

// The cells a raw identity byte's shape covers in its orientation.
[[nodiscard]] constexpr const NewPieceShape& newPieceShape(std::uint8_t raw) {
    assert(isNewPiece(raw) && raw < kNewModeRawEnd && "raw byte does not name a New-mode piece");
    return kNewPieceShapes[newPieceIndex(raw)][raw & 0x03];
}

// The board tile a shape's cells write when it locks — its own block, one per shape.
[[nodiscard]] constexpr std::uint8_t newPieceTile(std::uint8_t raw) {
    assert(isNewPiece(raw) && raw < kNewModeRawEnd && "raw byte does not name a New-mode piece");
    return static_cast<std::uint8_t>(kNewPieceTileBase + newPieceIndex(raw));
}

}  // namespace kirpich
