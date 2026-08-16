# Piece system

The active falling piece: spawning it, dropping it, rotating and shifting it, testing it against the
board, and locking it in. The behavioral specification — what the original game does, line by line — is
in [`../contracts/piece-system.md`](../contracts/piece-system.md); the design rationale is in
[`../features/piece-system.md`](../features/piece-system.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/piece.h` / `.cpp` | The `kirpich::systems` piece functions and `PieceCell`. |
| `src/systems/audio_cues.h` | `AudioCues` — the cue mailbox the piece routines write, a `GameContext` member. |
| `tests/test_piece_system.cpp` | The behavioral tests. |

The functions take a `GameContext&` (`src/systems/game_context.h`) and read or write through it. They
own no state: the active and preview slots live on `SpriteRendererState`, the drop and lock counters on
`GameFlowState`, the score and soft-drop total on `EngineState`, the board on `PlayingFieldState`, and
the audio cues on the `AudioCues` member. The active piece is always slot 0, the preview slot 1
(`kActivePieceSlot` / `kPreviewPieceSlot` in `src/state/sprite_renderer_state.h`).

## The surface

```cpp
namespace kirpich::systems {

// The four board cells the active piece covers: row/col index PlayingFieldState::board, tile is what
// the piece writes there when it locks.
struct PieceCell { std::uint8_t row, col, tile; };
BoundedVec<PieceCell, 4> activePieceCells(const GameContext& game);

// Whether the active piece overlaps a non-empty board cell where it is now.
bool detectCollision(const GameContext& game);

// Rotate per this frame's rotation presses, then shift left/right per the movement presses/holds with
// auto-repeat. Each stage cues its sound, tests for a collision, and reverts (cancelling the cue) if the
// new position collides.
void rotateAndShiftPiece(GameContext& game);

// Step the piece down one row — by gravity, or faster while soft drop is held — and, when the step
// collides, lock it: begin the lock, award soft-drop points, and end the game on a second spawn-lock.
void dropPiece(GameContext& game);

// Write the resting piece's four tiles into the board and finish the lock. Does nothing unless a lock is
// in progress (drop begins it); hides the active piece afterward.
void lockPieceIntoBackground(GameContext& game);

// Spawn the next piece: promote the preview into the active slot, then choose the new preview — from the
// shared piece list during a demo or two-player game, otherwise by drawing from `draw`. Reloads the drop
// timer. `draw` yields one candidate piece byte per call; it is consulted only on the random path.
void nextPiece(GameContext& game, const std::function<std::uint8_t()>& draw);

}
```

**Using them.** Each frame, given the game context, the gameplay code drives the piece — rotate and
shift under input, then drop:

```cpp
kirpich::systems::rotateAndShiftPiece(game);
kirpich::systems::dropPiece(game);
```

`dropPiece` reads `game.joypad` (this frame's held/pressed snapshot, set by the dispatcher) to decide
soft drop vs. gravity, and on a landing it begins the lock and may set `game.flow.gameState` to the
game-over state. Finish the lock and spawn the next piece:

```cpp
kirpich::systems::lockPieceIntoBackground(game);          // writes the tiles, advances the lock
// … the line-clear flow runs between lock and spawn …
kirpich::systems::nextPiece(game, drawByte);              // drawByte is a std::function<std::uint8_t()>
```

For live play, `drawByte` is the randomizer's registered routine (a `retropp::Routine<std::uint8_t()>`
is callable and binds straight to the `std::function`); see [`piece-random.md`](piece-random.md). Only
the random path (solo play) calls it — a demo or a two-player game walks the shared piece list instead.

To find where the piece is on the board (the line-clear scan, or a renderer):

```cpp
for (const auto& cell : kirpich::systems::activePieceCells(game)) {
    // game.field.board[cell.row][cell.col] is the cell; cell.tile is the piece's tile there.
}
```

## Gotchas

- **The board must be space-filled to play.** A cell collides when it is anything but the empty-space
  tile (`CharTile::SPACE`), and a boot board is all-zero — which collides everywhere. The playable field
  is filled with space during title-screen setup; `detectCollision` on a zero board reports a collision
  on every cell.
- **`dropPiece` reads the joypad snapshot, not raw input.** It uses `game.joypad.held` / `.pressed`. The
  dispatcher samples those once per frame before running a state's handler; call the piece functions from
  a handler, after the snapshot is set.
- **The audio cues accumulate.** Rotating and shifting write `game.audioCues.square`; a top-out writes
  `game.audioCues.wave` and `game.audioCues.resetRequested`. Nothing drains them yet — the audio tick
  will. A cue can be overwritten to `NONE` within the same frame (a colliding rotation cancels its cue),
  so read the mailbox as the last cue of the frame.
- **`nextPiece` takes the draw as a parameter.** It does not construct or hold the randomizer. Pass the
  same callable each frame; on the deterministic path it is never called.

## Changing behavior

- **The cell geometry** — the renderer position law and the tile-lookup cell map — is `cellForPart` in
  `src/systems/piece.cpp` (file-local). It reproduces the original's 8-bit `add`/`adc` carry and the
  hidden-piece row; change it only against the contract (§2), since collision and locking both depend on
  it.
- **The collision rule** is `detectCollision`: a cell collides when its board byte is not
  `CharTile::SPACE`.
- **The rotation and shift** are `rotateAndShiftPiece` — the four-orientation block-wrap, the
  right-over-left priority, and the shift's auto-repeat over `game.flow.keyRepeatTimer`. The auto-repeat
  timing itself is the shared `keyRepeatFire` core and its constants in `src/systems/input.h` (see
  [`input.md`](input.md)).
- **The drop timing** is `dropPiece`: the soft-drop cadence (`timer2 = 3`), the gravity period
  (`framesPerDrop`), the three soft-drop gates, and the top-out counter. The soft-drop point award is
  `softDropAward` in `src/data/scoring.h` (see [`scoring.md`](scoring.md)).
- **The next-piece choice** is `nextPiece`: the deterministic ring walk and the random path's
  repeat-rejection. The draw source and the fold are the randomizer's, in `src/vm/`
  (see [`piece-random.md`](piece-random.md)).

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^PieceSystem\.'
```

The tests are device-free: the functions are pure logic over the game-state aggregate, and the cell
geometry is checked against values hand-traced from the original's renderer and tile-lookup routines.
