# Contract — Core enums

Reverse-derived behavioral contract for Kirpich's core type surfaces: the seven fundamental values
the game logic, state, audio, serial, and rendering layers all branch on. Every value here is
transcribed from the `kaspermeerts/tetris` disassembly (upstream `b95c668`); the line anchors below
are the authority the tests check against.

The upstream is a flat disassembly — `constants.asm` is five lines and there is no constants tree —
so these values reach the port by three different routes, and the route determines how each is kept
honest:

- **Named constants** (`SerialRole`, `SerialClockMode`) — declared with `EQU` in `constants.asm`.
  Their headers are generated verbatim by `tools/asm_parser/parse_core_enums.py`.
- **Label / dispatch encoded** (`GameState`, `SerialState`) — the value is a jump-table index or a
  label's hex suffix. The parser scans the tables and writes a value fixture
  (`tests/fixtures/core_enums_expected.h`); the hand-written headers are checked against it.
- **Reverse-derived** (`GameType`, `MusicType`, `Piece`) — the value exists only as a bare byte at
  the code sites that use it, sometimes with the disassembler's own uncertain note. These are
  hand-written and pinned by the line anchors below.

---

## `GameState` — `include/kirpich/game_state.h`

`enum class GameState : uint8_t`. The main loop reads one byte each frame and jumps through a pointer
table to that state's handler (`tetris.asm:419-421` load + `rst $28`; table at `tetris.asm:423-476`).

- **54 states, contiguous `$00`–`$35`.** Each entry is `dw GameState_XX`; every dispatched value has
  a matching `GameState_XX::` handler label.
- **Names are port-authored** from the jump-table comments — the labels themselves carry only the
  hex index. Two states the disassembler could not explain keep an index name: `STATE_09_UNUSED`
  (`$09`, "just points to a random RET... what?") and `STATE_0C_UNKNOWN` (`$0C`, "?").
- **Vestigial slot.** The table has a 55th entry at index `$36` — `dw $27EA` (`tetris.asm:477`), a
  raw address, not a handler. It is a dispatch over-read and is **not** a state; it has no
  enumerator.

The 54 values are checked against the scanned fixture; the count, the absence of duplicates, and the
contiguity of `$00`–`$35` are all asserted.

## `SerialType`s

### `SerialRole` — `include/kirpich/serial_role.h`
`enum class SerialRole : uint8_t { MASTER = 0x29, SLAVE = 0x55 }`. `constants.asm:1-2`. Elected in
the handshake: a Game Boy that reads the `SLAVE` code off the wire becomes `MASTER`, and vice versa
(`tetris.asm:81-94`).

### `SerialClockMode` — `include/kirpich/serial_clock_mode.h`
`enum class SerialClockMode : uint8_t { EXTERNAL = 0x80, INTERNAL = 0x81 }`. `constants.asm:4-5`
(`SERIAL_TRANSFER_EXTERNAL_CLOCK` / `SERIAL_TRANSFER_INTERNAL_CLOCK`). Written to `rSC` to select
whether this side waits for the partner's clock (external) or drives it (internal).

### `SerialState` — `include/kirpich/serial_state.h`
`enum class SerialState : uint8_t`. The serial-interrupt dispatch (`tetris.asm:59-66`) indexes:

| Value | Name | Upstream |
|---|---|---|
| `0x00` | `HANDSHAKE` | `dw Handshake` — master/slave election |
| `0x01` | `RECEIVE` | `dw SerialState_01` (`tetris.asm:100`) |
| `0x02` | `EXCHANGE` | `dw SerialState_02` (`tetris.asm:105`) |
| `0x03` | `ACKNOWLEDGE` | `dw SerialState_03` (`tetris.asm:119`) |

**Vestigial slot.** Dispatch index 4 is `dw LoadTilesFromHL.ret` — a bare `ret` the upstream tags
`XXX Is this used?` (`tetris.asm:66`). The running game never selects it; no enumerator.

## `GameType` — `include/kirpich/game_type.h`

`enum class GameType : uint8_t { TYPE_A = 0x37, TYPE_B = 0x77 }`. Two non-sequential magic bytes the
game compares directly: `cp a, $37 ; A-Type` (`tetris.asm:3273`), `cp a, $77 ; B-Type`
(`tetris.asm:3280`); also the demo init `ld a, $37 ; ... 37 = A-Type, 77 = B-Type`
(`tetris.asm:583-584`). **Exactly two values.** Two-player mode is not a game type — it is a separate
flag (`hIsMultiplayer`, e.g. `tetris.asm:588`) and is orthogonal to this enum.

## `MusicType` — `include/kirpich/music_type.h`

`enum class MusicType : uint8_t { MUSIC_A = 0x1C, MUSIC_B = 0x1D, MUSIC_C = 0x1E, OFF = 0x1F }`. The
stored byte is the music-select cursor's sprite tile, not a `0..3` index. The selection screen moves
the cursor across tiles `$1C`–`$1F` (bounds at `tetris.asm:3202-3232`); `SwitchMusic`
(`tetris.asm:3249-3255`) turns the byte into a song by `sub a, $17`, and treats offset `$08`
(i.e. `$1F`) as "music off" (`ld a, $FF`).

## `Piece` — `include/kirpich/piece.h`

`struct Piece { uint8_t raw; }` with `kind() = PieceKind(raw >> 2)` and `rotation() = raw & 0x03`;
`static_assert(sizeof(Piece) == 1)`. The game packs a piece's kind and orientation into one byte and
separates them by masking (`and a, ~%11` isolates the kind — "the lower two bits are used for
orientation", `tetris.asm:5088`). `PickRandomPiece` rerolls until the kind byte is below `7 * 4`
(`tetris.asm:1584`), so there are 7 kinds (`0..6`) × 4 rotations (`0..3`), a valid `raw` range of
`0..27`. **No sentinel** — there is no "no piece" value in this byte.

This is the piece-logic byte (what the RNG and piece routines manipulate), distinct from the tile
indices the renderer later uses. `kind()` returns `PieceKind` (`include/kirpich/piece_kind.h`) — the
seven tetromino shapes in the game's assignment order (`L, J, I, O, S, Z, T`). That ordering and its
source anchors are recorded in [`sprite-grids.md`](sprite-grids.md) § `PieceKind`.

---

## Tested by

`tests/test_core_enums.cpp` — value assertions for every enumerator, the `GameState`/`SerialState`
drift checks against `tests/fixtures/core_enums_expected.h`, and the full `Piece` decode/round-trip
sweep over `raw = 0..27`. The parser's own structural checks
(`tools/asm_parser/test_parse_core_enums.py`) guard the scan against upstream changes.
