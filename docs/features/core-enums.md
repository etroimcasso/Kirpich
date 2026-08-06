# Core enums

The floor of the data layer: the seven fundamental type surfaces the rest of the game is written in
terms of. The game-state machine jumps on `GameState`; single-player logic branches on `GameType`;
audio selects on `MusicType`; the link-cable stack uses the three `Serial*` types; and every piece
routine manipulates a `Piece` byte. Porting them first means everything downstream imports a typed
value instead of a bare byte.

## What they are

| Type | Header | Shape |
|---|---|---|
| `GameState` | `include/kirpich/game_state.h` | `enum class : uint8_t`, 54 states `$00`–`$35` |
| `GameType` | `include/kirpich/game_type.h` | `enum class : uint8_t`, `TYPE_A` / `TYPE_B` |
| `MusicType` | `include/kirpich/music_type.h` | `enum class : uint8_t`, `MUSIC_A`..`MUSIC_C` / `OFF` |
| `SerialRole` | `include/kirpich/serial_role.h` | `enum class : uint8_t`, `MASTER` / `SLAVE` |
| `SerialClockMode` | `include/kirpich/serial_clock_mode.h` | `enum class : uint8_t`, `EXTERNAL` / `INTERNAL` |
| `SerialState` | `include/kirpich/serial_state.h` | `enum class : uint8_t`, four dispatch states |
| `Piece` | `include/kirpich/piece.h` | 1-byte struct, `kind()` + `rotation()` |

The exact values and their sources are pinned in [`../contracts/core-enums.md`](../contracts/core-enums.md).

## Decisions

**Values come straight from the original; names are chosen here.** The original stores these as raw
bytes — often non-sequential ones (`GameType` is `$37`/`$77`, `MusicType` is a cursor sprite tile).
The port keeps the exact bytes and gives them readable names. Where the original had no name to
borrow, the name is chosen to describe what the value does.

**`GameState` names come from the disassembly's own annotations.** The 54 states are labelled only by
hex index upstream; the readable names are taken from the jump-table comments. A couple of states
that the original's authors flagged as unexplained keep a deliberately honest index-based name
(`STATE_09_UNUSED`, `STATE_0C_UNKNOWN`) rather than a guess.

**`GameType` has two values, not three.** It is tempting to fold two-player into the same enum, but
the original tracks two-player mode with a separate flag; game type stays `TYPE_A` / `TYPE_B`.

**`Piece` is a byte, not two fields.** The original packs a piece's kind and rotation into one byte
and pulls them apart by masking. The port mirrors that exactly with a one-byte struct and accessors,
so it stays byte-identical to what the piece and randomization routines expect. `kind()` returns a
`PieceKind` — the named tetromino shapes; which index is which is fixed by the sprite layout grids
(see [`sprite-grids.md`](sprite-grids.md)).

**Two vestigial slots are documented, not ported as states.** Both the game-state and serial dispatch
tables have a trailing entry that points at a stray address or a bare return and that the running
game never selects. They are recorded in the contract and left out of the enums.

## Keeping them honest

The values that the original declares as named constants, and the value sets that live in its jump
tables, are read directly out of the disassembly by `tools/asm_parser/parse_core_enums.py`, which
also writes a fixture the tests check the hand-written headers against. So a change to the original
(or a typo in a header) shows up as a failing test rather than a silent divergence. See
[`../engine/core-enums.md`](../engine/core-enums.md) for how to regenerate and extend them.
