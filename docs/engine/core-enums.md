# Core enums

The fundamental value types the rest of Kirpich is written against: game state, game type, music
type, the three serial types, and the piece byte. All live as header-only types in
`include/kirpich/`; there is no `.cpp` and no lookup table behind them.

## Where each type lives

| Type | Header | Editing it |
|---|---|---|
| `GameState` | `game_state.h` | Hand-written names, values checked against a generated fixture (below). |
| `GameType` | `game_type.h` | Hand-written. |
| `MusicType` | `music_type.h` | Hand-written. |
| `SerialState` | `serial_state.h` | Hand-written names, values checked against the fixture. |
| `SerialRole` | `serial_role.h` | **Generated — do not hand-edit.** |
| `SerialClockMode` | `serial_clock_mode.h` | **Generated — do not hand-edit.** |
| `Piece` | `piece.h` | Hand-written. |

All are in `namespace kirpich`. Include them as `<kirpich/game_state.h>` and so on.

## `Piece`

`Piece` wraps one byte:

```cpp
kirpich::Piece p{0x0A};   // raw = kind*4 + rotation
p.kind();                 // PieceKind::I  (0x0A >> 2 == 2)
p.rotation();             // 0x0A & 3  == 2   (0..3)
kirpich::Piece::of(2, 2); // build from kind + rotation
```

`sizeof(Piece) == 1` is asserted at the definition. Valid `raw` runs `0..27` (7 kinds × 4 rotations);
there is no "empty" value. `kind()` returns a `PieceKind` — the seven tetromino shapes (`L, J, I, O, S,
Z, T`); see [sprites.md](sprites.md).

## Regenerating the generated headers

`serial_role.h`, `serial_clock_mode.h`, and the test fixture `tests/fixtures/core_enums_expected.h`
are produced from the disassembly by the parser. Regenerate them after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_core_enums.py \
  --source-root ../tetris \
  --all \
  --serial-role-out       include/kirpich/serial_role.h \
  --serial-clock-mode-out include/kirpich/serial_clock_mode.h \
  --fixture-out           tests/fixtures/core_enums_expected.h
```

The parser checks the source's structure as it reads — the serial constants, the 54 game states and
their trailing over-read slot, and the serial dispatch order — and stops with a citation if anything
has moved, rather than emitting a wrong file. Python 3 (standard library only); it is a development
tool and is never needed to build or test Kirpich.

## Changing values

To change a **hand-written** enum, edit its header. If you change a `GameState` or `SerialState`
value, the drift check in `tests/test_core_enums.cpp` compares your header against the generated
fixture, so a value that no longer matches the source fails the test — regenerate the fixture (above)
if the source genuinely changed, or fix the header if it was a mistake.

To change a **generated** header, change nothing here — edit the parser or the source and regenerate;
a hand edit is overwritten on the next run.

The exact meaning of every value, with its line in the original, is in
[`../contracts/core-enums.md`](../contracts/core-enums.md).

## Testing

`tests/test_core_enums.cpp` asserts every value, drift-checks `GameState` and `SerialState` against
the fixture, and sweeps every `Piece` decode over `raw = 0..27`. The parser has its own tests
(`tools/asm_parser/test_parse_core_enums.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_core_enums`).
