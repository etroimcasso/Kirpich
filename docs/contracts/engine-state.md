# Contract — Engine state (WRAM globals)

Reverse-derived behavioral contract for `EngineState` (`src/state/engine_state.h`): the block of work
RAM the original game keeps at `$C000`, ported as one C++ struct. The layout is transcribed from
`tetris/wram.asm`; the three unlabelled flags and every use-site line below are from `tetris/tetris.asm`
(upstream `b95c668`). The line anchors are the authority the tests check against.

`EngineState` is an idiomatic surface, not a byte image: the struct chooses C++ shapes for the way the
gameplay systems read the data, and does **not** mirror the ROM's byte offsets. Byte-level fidelity to
the layout is held by the fixture (`tests/fixtures/wram_expected.h`) and the widths pinned in
`tests/test_engine_state.cpp`, not by the struct's memory image.

---

## Field map

Every row's address and size come from `wram.asm` (via `tools/asm_parser/parse_wram.py`); the port type
is the hand-written shape in `engine_state.h`.

| Port field | ROM label | Address | ROM size | Port type | Notes |
|---|---|---|---|---|---|
| `oam` | `wOAMBuffer` | `$C000` | 160 | `std::array<OamEntry, 40>` | 40 objects × 4 bytes; attribute byte unpacked (below) |
| `score` | `wScore` | `$C0A0` | 3 | `uint32_t` | packed-decimal in ROM; decimal integer port-side, 999,999 display ceiling |
| `lineClears` | `wLineClearsList` | `$C0A3` | 9 | `BoundedVec<uint8_t, 4>` | four row addresses + a zero-word terminator; ported as row indices (below) |
| `stats.singles` | `wLineClearStats` / `wSinglesCount` | `$C0AC` | 1 | `uint8_t` | head of the stats block (the two labels alias one address) |
| `stats.doubles` | `wDoublesCount` | `$C0B1` | 1 | `uint8_t` | stride 5 (byte +1 is a display count, +2..+4 a render-derived accumulator — below) |
| `stats.triples` | `wTriplesCount` | `$C0B6` | 1 | `uint8_t` | |
| `stats.tetrises` | `wTetrisCount` | `$C0BB` | 1 | `uint8_t` | |
| `scoreboardDisplayedStats` | *(byte +1 of each stat block)* | `$C0AD` … `$C0BC` | 1 ×4 | `LineClearStats` | the four on-screen results counts; single-byte BCD in ROM, decimal port-side |
| `softDropPoints` | `wSoftDropPoints` | `$C0C0` | 2 | `uint16_t` | 16-bit binary count |
| `softDropPointsTallied` | `wSoftDropPointsBCD` | `$C0C2` | 3 | `uint16_t` | count-up display of drained soft-drop points; 3-byte BCD in ROM, decimal port-side |
| `scoreboardState` | `wScoreboardState` | `$C0C5` | 1 | `uint8_t` | raw state-machine index |
| `scoreboardTallyPhase` | *(unlabelled)* | `$C0C6` | 1 | `uint8_t` | folded flag (below) |
| `blockSoftDropAfterLock` | *(unlabelled)* | `$C0C7` | 1 | `bool` | folded flag (below) |
| `scoreRedrawRequested` | *(unlabelled)* | `$C0CE` | 1 | `bool` | folded flag (below) |
| `hidePreviewPiece` | `wHidePreviewPiece` | `$C0DE` | 1 | `bool` | written 0/1 |
| `pieceList` | `wPieceList` | `$C300` | 256 | `std::array<Piece, 256>` | the randomizer's piece ring; each byte a packed `Piece` |

The layout fixture covers the **whole** of `wram.asm`, including the top-scores block
(`wTypeBTopScores` `$D000`, `wTypeATopScores` `$D654`) and the Audio-RAM section (`$DF70`+). Those rows
are not part of `EngineState`; they belong to later state units and are carried here so the same
fixture serves them.

## OAM attribute unpack

Each `wOAMBuffer` object is four bytes: `y`, `x`, `tile`, `attr`. `OamEntry` keeps `y`, `x`, `tile`
verbatim and unpacks the DMG object-attribute byte into four named flags:

| Bit | Flag | Meaning |
|---|---|---|
| 7 | `behindBg` | object drawn behind background colours 1–3 |
| 6 | `yflip` | vertical flip |
| 5 | `xflip` | horizontal flip |
| 4 | `palette1` | use object palette 1 (OBP1) instead of 0 |

Bits 3–0 are the CGB bank/palette bits and are unused on the DMG target. `OamEntry` carries the full
attribute contract because live staging can set any of these bits; the static object tables in the
data layer (`OamObject` in `src/data/misc.h`) only ever set X-flip and keep the narrower shape. The two
types are distinct by role — a live staging entry versus a static table row.

## Line-clears list — addresses become indices

`wLineClearsList` stores up to four **field-row addresses** as `dw` values, terminated by a zero word
(the ninth byte). `lineClears` stores the equivalent **row indices** 0–17 and uses `size()` in place of
the terminator. The address↔index relation is the playing-field geometry: the field's top row is at
`$C802` with a `$20`-byte row stride (see [`playing-field.md`](playing-field.md)), so
`index = (address − $C802) / $20`. Dropping the address space is a forced adaptation — the port has no
`$C8xx` addresses — and the conversion belongs to the line-clear code that fills the list.

## Collapsed and render-derived bytes

- **`wSoftDropPointsBCD` (`$C0C2`, 3 bytes) is the field `softDropPointsTallied`.** It is the count-**up**
  display of soft-drop points already drained into the score during the Type B results tally: it rises
  (`AddBCD` +1, `tetris.asm:4861-4864`) as `softDropPoints` falls, and the two sum to the pre-tally
  total. It is not derivable from `softDropPoints` alone, so it is carried as state (decimal port-side,
  3-byte BCD only on the wire).
- **The stride-5 stat pads are not dead.** Each of the four counts is followed by four bytes. Byte +1
  (`$C0AD`/`$C0B2`/`$C0B7`/`$C0BC`) is the on-screen results count, carried as `scoreboardDisplayedStats`
  (written `tetris.asm:6119-6122`). Bytes +2..+4 are a per-kind 3-byte BCD score accumulator (written
  `:6136-6141`, printed `:6143-6149`); the port does **not** store them — they equal
  `kLineClearScores[kind].points × (typeBLevel + 1) × scoreboardDisplayedStats[kind]`, which the render
  bridge re-derives. The derivability proof and its unreachable-saturation argument live in
  [`scoring-system.md`](scoring-system.md).

## The three folded flags

The RAM between `wScoreboardState` and `wHidePreviewPiece` holds three single-byte flags the
disassembly reads and writes but never labels. They are named here for their role and anchored to their
use sites:

| Field | Address | Anchor(s) | Role |
|---|---|---|---|
| `scoreboardTallyPhase` | `$C0C6` | stored `tetris.asm:4713`, `:4846`, `:6162`, `:6172`, `:6181`; read `:4881`, `:6110` | which phase of the score/line count-up animation is running (values 0/1/2) |
| `blockSoftDropAfterLock` | `$C0C7` | comment `tetris.asm:5168` "Flag to avoid soft dropping after the last piece locked"; stored `:5176`, `:5236`, `:5748` | suppress soft-drop scoring immediately after a piece locks |
| `scoreRedrawRequested` | `$C0CE` | comment `tetris.asm:236` "Score needs updating?"; read `:236`; stored `:249`, `:5298` | the score display needs to be redrawn |

`scoreboardTallyPhase` and `scoreboardState` are kept as raw indices; naming their value sets is left to
the scoreboard/scoring code that owns those state machines.

## Lifecycle

Every field is zero-initialised, so a default-constructed `EngineState` is the boot state and `reset()`
returns a live instance to it. Per-field game-start initialisation is **not** state lifecycle and is not
done here — it is the job of the systems that own the fields:

- The randomizer fills `pieceList` at game start (`wPieceList` written at `tetris.asm:485`, `:999`,
  `:1403`, …). One consumer reads the ring's last slot as `wPieceList + $FF` (`tetris.asm:1651`); that
  index math stays with the consumer, not the state surface.
- `oam` is rebuilt every frame before the display flush (`wOAMBuffer` at `:559`, `:717`, `:1673`, …).

The original sets the stack pointer with `ld sp, $CFFF` (`tetris.asm:309`, upstream comment "TODO top of
RAM"). That is a hardware-stack detail with no port-side field; it is a native no-op, noted here only so
the `$CFxx` region's use is accounted for.

---

## The surface

- **Parser-emitted** (`tools/asm_parser/parse_wram.py`, `--all`): `tests/fixtures/wram_expected.h` — the
  `{name, address, size}` layout row for every label plus the anonymous gaps, with a section-tiling
  proof. Layout only; there are no ROM data values in this unit. Regenerate after any upstream repin; do
  not hand-edit.
- **Hand-written port-design:** `src/state/engine_state.h` (the `EngineState`, `OamEntry`, and
  `LineClearStats` structs and `reset()`), the tests, and this contract.

### Transcription asserts

`parse_wram.py` hard-errors (with a `file:line` citation) on any of: an `ds $HIGH − $LOW` gap whose
`$LOW` is not the running address; a `SECTION … WRAM0[$XXXX]` whose origin the preceding section did not
tile up to; an unrecognised directive; a region of non-positive size or one outside `$C000`–`$DFFF`; or a
section whose regions do not tile it exactly. The walk derives every address from the section origins and
the reservations that precede each label — no address is hand-typed.

---

## Tested by

`tests/test_engine_state.cpp` — the fixture-integrity sweep (every `wram.asm` region parses, addresses
strictly ascending, sizes positive, each section tiled with no overlap or hole); the width/count pins
tying the `$C000` rows to the struct (`wOAMBuffer` 160 = 40 × 4 ↔ `oam`; `wScore` 3 bytes ↔ the decimal
ceiling note; `wPieceList` 256 ↔ `pieceList`; `wLineClearsList` nine bytes for a four-entry cap; the four
stat counts; `wSoftDropPoints` 2 bytes ↔ `uint16_t`); the reset-to-zero behavioural test (mutate every
field, `reset()`, compare against a fresh instance); the `OamEntry` aggregate and `operator==` pins; and
the `lineClears` bounded-domain pin. The parser's own structural checks
(`tools/asm_parser/test_parse_wram.py`) guard the walk against upstream changes.
