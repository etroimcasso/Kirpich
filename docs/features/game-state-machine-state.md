# Game-flow state

The bookkeeping the main loop lives in — the state-machine index it dispatches on every frame, the
frame and wipe counters, the drop-timing scalars, the menu selections, and the piece-pipeline counters
— ported as one hand-written C++ struct. Where the global game state holds the score, the sprite
buffer, and the piece ring, this holds the state the main loop itself reads to decide what to do next.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `GameFlowState` | `src/state/game_flow_state.h` | the 27 game-flow bytes: the `gameState` dispatch index, the frame/wipe/drop timers, the level and menu selections, the piece-pipeline counters, and the two bytes shared with the top-score pointer |

The behavioral specification — the field-by-field mapping to the original's HRAM addresses, the
per-byte ownership of the whole `$FF80` map, the two shared bytes, and the traced widths of the two
flag fields — is in [`../contracts/game-state-machine-state.md`](../contracts/game-state-machine-state.md).

## Decisions

**One header-only struct, a sibling of the global state.** `GameFlowState` is a plain struct with a
`reset()` that returns it to the all-zero boot state; there is no `.cpp`. It sits beside the global
game-state struct rather than inside it — each state block is its own type, and combining them into the
running game is later wiring.

**The `$FF80` map is drawn per byte, not per label.** Unlike the `$C000` block, the original's high
RAM has many live bytes with no label — seven slots are commented out, and several gaps hold state the
game reaches by a raw numeric address (`ldh [$98], a`) rather than a name. Four of those unlabelled
bytes are game-flow state and become named fields here (`pieceLockStage`, `blinkCounter`,
`completedRowCount`, `coarseCountdown`); the rest belong to other state surfaces, to engine mechanism,
or are dead. The boundary is settled byte by byte in the contract.

**`lines` is decimal.** The original keeps the line count as a two-byte packed-decimal value; the port
stores a decimal `uint16_t` and keeps packed decimal only on the wire, the same choice the score makes.

**Two bytes are shared with the top-score pointer.** Two HRAM bytes carry a gameplay purpose and a
top-score-entry purpose that never overlap in time. The original overlays both on one byte; the port
carries an independent field in each surface. This unit takes the gameplay halves — `tempPreviewPiece`
(`$FFFC`) and `topOutLockCount` (`$FFFB`, the topout piece counter that forces game over) — and leaves
the pointer halves to the top-score surface.

**Two flag widths were traced, not guessed.** `paused` is a `bool` — the original only ever writes
`1`, toggles bit 0, or clears it, so the domain is `{0, 1}`. `heartMode` is a `uint8_t` — the original
stores the raw joypad byte in it and reads it only as zero / non-zero, so its non-zero value is not a
single constant.

**Menu selections are typed but boot "unset".** `gameType` and `musicType` are the existing enum types;
their boot value `0` is not a valid enumerator, mirroring the original, where the byte is meaningless
until a menu writes it.

## Keeping it honest

The struct does not mirror the original's byte offsets, so its fidelity is checked two ways. A parser
(`tools/asm_parser/parse_hram.py`) walks the original's whole high-RAM map and emits a layout fixture —
every label with its address and size — deriving each address from the section origin and the space
reserved before it, and stopping with a source citation if the map's own arithmetic does not line up.
The same parser scans the game code for every raw-operand access to high RAM and emits a **census** of
those addresses. `tests/test_game_flow_state.cpp` proves the layout tiles the section with no overlap
or hole, pins the struct's field widths against the fixture, and — the key check — resolves **every**
censused address to exactly one owner, so a raw access to a byte no surface claims fails the build. See
[`../engine/game-state-machine-state.md`](../engine/game-state-machine-state.md) for how to use and
regenerate it.

## Not here yet

The state block is data; the code that mutates it — the main loop that dispatches on `gameState` and
decrements the timers, the gravity system that reads the drop scalars, the menus that write the
selections, the line-clear path that updates `lines` and the lock counters — is gameplay work that
builds on this struct. The layout fixture and its census are authored to serve the later state surfaces
too (the sprite renderer, the serial/multiplayer link, the demo recorder, the top scores), each of
which claims its own bytes of the same map. This unit provides the struct, its lifecycle, and the
fixture to check it against.
