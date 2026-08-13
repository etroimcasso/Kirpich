# Game-flow state

The bookkeeping the main loop lives in — the state-machine index it dispatches on, the frame and wipe
counters, the drop-timing scalars, the menu selections, and the piece-pipeline counters — held in one
`GameFlowState` struct. A single instance carries what the main loop reads each frame to decide what to
do next; the gameplay systems take a reference to it, read the fields they care about, and write the
ones they own.

It is an idiomatic C++ surface, not a byte image of the original's high RAM. `lines` is a decimal
integer, `paused` is a `bool`, and the menu selections are typed enums. The exact mapping back to the
original's `$FF80` addresses — and the per-byte split between this surface and the others that share
the same RAM — is the behavioral spec in
[`../contracts/game-state-machine-state.md`](../contracts/game-state-machine-state.md).

Everything is in `namespace kirpich`, header-only, included as `"state/game_flow_state.h"` (the `src/`
tree is on the library's include path). There is no `.cpp`. It is a sibling of `EngineState`
([`engine-state.md`](engine-state.md)), not a member of it.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/state/game_flow_state.h` | `GameFlowState` and `reset()` | Hand-written. Edit here to add or reshape a field. |
| `tests/fixtures/hram_expected.h` | The original's whole high-RAM layout map (`{name, address, size}` per label) and the raw-operand access census (`{address, refCount}`) | **Generated — do not hand-edit.** |

## The type

`GameFlowState` is the game-flow block — 27 fields, each mapping to one high-RAM byte (or, for `lines`,
two):

```cpp
struct GameFlowState {
    uint8_t  pieceLockStage = 0;     // lock-delay stage of the piece coming to rest
    uint8_t  dropTimer = 0;          // frames until the piece steps down one row
    uint8_t  framesPerDrop = 0;      // gravity period for the current level
    uint8_t  blinkCounter = 0;       // cursor / entry blink phase
    uint16_t lines = 0;              // lines cleared, decimal
    uint8_t  completedRowCount = 0;  // rows completed by the current lock
    uint8_t  timer1 = 0, timer2 = 0; // general frame timers, saturating auto-decrement
    uint8_t  level = 0;              // current level
    uint8_t  keyRepeatTimer = 0;     // DAS auto-repeat frame counter
    bool     paused = false;         // pause flag
    Piece    nextPreviewPiece{};     // the piece after the preview
    uint8_t  numPiecesPlayed = 0;    // pieces played (incremented only when determinism matters)
    GameType  gameType{};            // Type A / Type B (boot value 0 is "unset")
    MusicType musicType{};           // music choice (boot value 0 is "unset")
    uint8_t  typeALevel = 0, typeBLevel = 0, typeBStartHeight = 0;   // menu selections
    uint8_t  coarseCountdown = 0;    // counts timer1 expiries
    GameState gameState{};           // the main-loop dispatch index (boot NORMAL_GAMEPLAY)
    uint8_t  frameCounter = 0;       // +1 every VBlank
    uint8_t  wipeCounter = 0;        // playing-field wipe animation step
    uint8_t  softDropCounter = 0;    // frames the piece has been soft-dropped
    SpriteId rocketSpriteIndex{};    // score-tier rocket sprite id
    uint8_t  heartMode = 0;          // 0 = normal, non-zero = heart mode
    Piece    tempPreviewPiece{};     // staged preview piece (shares a byte with the top-score pointer)
    uint8_t  topOutLockCount = 0;    // topout piece counter (shares a byte with the top-score pointer)

    void reset();
    friend bool operator==(const GameFlowState&, const GameFlowState&) = default;
};
```

Every member is zero-initialised, so a default-constructed instance is the boot state, and the struct
has a defaulted `==`.

## Using it

```cpp
#include "state/game_flow_state.h"

kirpich::GameFlowState flow;   // all-zero: the boot state

// The main loop dispatches on gameState and transitions by writing a new value.
if (flow.gameState == kirpich::GameState::TITLE_SCREEN) { /* ... */ }
flow.gameState = kirpich::GameState::INIT_GAME;

// Read and write fields directly.
flow.frameCounter++;
flow.level = 9;
flow.paused = !flow.paused;

// Return everything to the boot state (e.g. when starting a new game).
flow.reset();
```

`reset()` restores every field to its default (all-zero) value; it is equivalent to assigning a fresh
`GameFlowState{}`.

## Gotchas

- **`lines` is a decimal integer, not packed-decimal.** The original stores it as two packed-decimal
  bytes; `GameFlowState` keeps a plain `uint16_t`. The Type A `9999` ceiling and the Type B down-count
  are enforced by the line-clear code, not by this struct.
- **`paused` is a `bool`, `heartMode` is a `uint8_t`.** Both are flags, but they were sized from how
  the original writes them: `paused` only ever holds `0` or `1`; `heartMode` holds the raw joypad byte
  and is read only as zero / non-zero, so a `bool` would lose information the original keeps.
- **`gameType` and `musicType` boot to an invalid enumerator.** Their boot value `0` is not a named
  `GameType`/`MusicType`; it means "unset until the menu writes it," exactly as in the original. Do not
  treat a freshly-reset `gameType` as `TYPE_A`.
- **`topOutLockCount` and `tempPreviewPiece` share their bytes with the top-score pointer.** Each sits
  on a byte the top-score code also uses, at a different time. They are independent fields here; the
  pointer halves live in the top-score state. Writing one does not corrupt the other because the two
  uses never overlap in time — see the contract.
- **This is the state block, not the logic.** Nothing in this struct decrements the timers or steps the
  state machine — reading and transitioning `gameState`, ticking the timers, and stepping the piece
  pipeline are the main loop's job, and they take a reference to a `GameFlowState` to do it.

## Regenerating the layout + census fixture

`hram_expected.h` is produced from the disassembly's high-RAM map and game code. Regenerate it after
repinning the upstream source:

```sh
python3 tools/asm_parser/parse_hram.py \
  --source-root ../tetris \
  --all \
  --fixture-out tests/fixtures/hram_expected.h
```

The parser walks the high-RAM map deriving every label's address and size, then scans the game code for
every raw-operand high-RAM access. It stops with a source citation if an address-arithmetic gap does
not line up, a directive is unrecognised, a region is out of range, the walk does not end at the top of
high RAM, or it meets a raw-access operand form it does not recognise. Python 3 (standard library
only); it is a development tool and is never needed to build or test Kirpich.

## Changing it

To add or reshape a game-flow field, edit `src/state/game_flow_state.h` and give the new member a zero
default so `reset()` and the default constructor stay correct. If the field maps to a labelled address
in the original, its width is pinned against the generated fixture, so add the mapping to
[`../contracts/game-state-machine-state.md`](../contracts/game-state-machine-state.md). If the field
maps to a byte the original reaches by a raw numeric address, that address appears in the census, and
the ownership check in the test requires the contract to name its owner — update the ownership map in
the contract in the same change.

## Testing

`tests/test_game_flow_state.cpp` sweeps the whole high-RAM layout fixture — every label and gap,
proving the section tiles with no overlap or hole and carries exactly one alias — and pins the struct
field widths against it. Its central check resolves every censused raw-operand address to exactly one
owner, so a raw access to an unclaimed byte fails the build. It also checks `reset()` returns a fully
mutated instance to the boot state, the typed-member boot values, and that the four unlabelled fold-in
bytes are genuinely raw-accessed and label-less. The parser has its own tests
(`tools/asm_parser/test_parse_hram.py`, run with `python3 -m unittest tools.asm_parser.test_parse_hram`).
