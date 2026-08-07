# Contract — Playing field

Reverse-derived behavioral contract for Kirpich's playing field: its fixed extent, and the schedule
that redraws it one row per frame after the stack changes. Every value here is transcribed from the
`kaspermeerts/tetris` disassembly (upstream `b95c668`); the line anchors below are the authority the
tests check against.

The field is **18 rows by 10 columns**. It lives twice in the original: as a shadow copy in work RAM
that the game logic edits, and as the tiles on screen. A **wipe** copies the shadow field back to the
screen a row at a time — used after a line clear compacts the stack, and again for the game-over
curtain — so the redraw is visible as a bottom-to-top sweep rather than a single instant repaint.

---

## Geometry

| Fact | Value | Anchor |
|---|---|---|
| Rows | 18 | count of wipe routines, one per row |
| Columns | 10 | `ld b, 10` in `WipePlayingFieldRow::` (`tetris.asm:5897`) |
| On-screen origin | BG map row 0, column 2 (`$9802`) | `PlayingFieldWipe19` destination (`tetris.asm:5749`) |
| Shadow-field origin | `$C802` | `PlayingFieldWipe19` source (`tetris.asm:5750`) |
| Row stride | `$20` (one BG-map row) | difference between consecutive wipe destinations |

The field sits at column 2 of the background map, so each 10-wide row occupies map columns 2–11 and a
full row is `$20` bytes further on than the row above it in both the on-screen map and the shadow
copy. The port keeps the extent — `kPlayingFieldRows`, `kPlayingFieldCols` — and the counter domain;
the addresses are the original's memory map and are not part of the port surface (see *Parser-emitted
vs. hand-written*).

## The wipe schedule

The original does not store the wipe as a table. It stores **18 near-identical routines**,
`PlayingFieldWipe02::` through `PlayingFieldWipe19::` (`tetris.asm:5563-5758`), above which the source
comments: *"Absolute garbage. I wonder if they used a macro..."*. Each routine has the same shape:

```
PlayingFieldWipe07::
    ldh a, [hWipeCounter]   ; gate: run only when the counter equals this routine's number
    cp a, 7
    ret nz
    ld hl, $9982            ; BG-map destination for this row
    ld de, $C982            ; shadow-field source for this row
    call WipePlayingFieldRow
    ret
```

`WipePlayingFieldRow::` (`tetris.asm:5896`) copies **one 10-byte row** from the source to the
destination and then **increments the counter** (`tetris.asm:5905-5907`).

The per-frame tick (`tetris.asm:214-232`) calls all 18 routines every frame, in **descending order
19 → 2**. Because each routine gates on the counter matching its own number, at most one body runs per
frame. So a counter set to 2 makes the field redraw one row per frame, **bottom to top**, over 18
frames; each row's copy bumps the counter to the next routine. `PlayingFieldWipe19` — the top row —
clears the counter to 0 (`tetris.asm:5752-5753`), ending the walk.

### Closed form

Every routine's address pair is exactly determined by its counter value `n`:

```
VRAM destination = $9802 + (19 - n) * $20
WRAM source      = $C802 + (19 - n) * $20
```

So counter 2 draws the bottom row and counter 19 the top, and the on-screen and shadow addresses
advance in lockstep (`vram - $9802 == wram - $C802`). The **row index** a counter redraws (0 = top,
17 = bottom) is `19 - n` — which is what `playingFieldRowForWipeCounter` returns.

| Counter | VRAM | WRAM | Row |
|---|---|---|---|
| 2 | `$9A22` | `$CA22` | 17 (bottom) |
| 3 | `$9A02` | `$CA02` | 16 |
| 4 | `$99E2` | `$C9E2` | 15 |
| 5 | `$99C2` | `$C9C2` | 14 |
| 6 | `$99A2` | `$C9A2` | 13 |
| 7 | `$9982` | `$C982` | 12 |
| 8 | `$9962` | `$C962` | 11 |
| 9 | `$9942` | `$C942` | 10 |
| 10 | `$9922` | `$C922` | 9 |
| 11 | `$9902` | `$C902` | 8 |
| 12 | `$98E2` | `$C8E2` | 7 |
| 13 | `$98C2` | `$C8C2` | 6 |
| 14 | `$98A2` | `$C8A2` | 5 |
| 15 | `$9882` | `$C882` | 4 |
| 16 | `$9862` | `$C862` | 3 |
| 17 | `$9842` | `$C842` | 2 |
| 18 | `$9822` | `$C822` | 1 |
| 19 | `$9802` | `$C802` | 0 (top) |

## The counter

`hWipeCounter` drives the schedule. Its domain:

| Value | Meaning |
|---|---|
| 0 | idle — no wipe in progress; every routine's gate fails |
| 1 | a line-clear compaction is pending (not a wipe row) |
| 2–19 | the wipe walk — the value is the routine that runs this frame |

**Who sets it:**

- **2** starts a wipe. Set by the line-clear compaction as it finishes (`ld a, 2`,
  `tetris.asm:5548`), by the game-over curtain (`FillPlayingFieldAndWipe::`, `tetris.asm:5041-5042`),
  and by several screen-transition paths.
- **1** is set by the line-clear animation as it completes (`tetris.asm:5463-5464`, alongside a
  13-frame `hTimer1`). It gates `MoveBlocksDownAfterLineClear::` (`tetris.asm:5498-5503`), which
  compacts the stack and then sets the counter to 2 to launch the wipe.
- **2 through 19** are set by `WipePlayingFieldRow` itself, one per copied row, walking the counter up
  the field until `PlayingFieldWipe19` clears it back to 0.

The counter is state, not part of this unit's surface; it is recorded here as context for the gameplay
and presentation code that will own it.

## Per-row side effects

Five of the routines do more than copy a row. These are **behavior, not geometry** — they belong to
the gameplay and presentation code and are recorded here only so that code has a specification. The
source flags several of them as bugs, and those quirks are preserved rather than corrected:

| Routine | Extra behavior | Anchor | Note |
|---|---|---|---|
| `PlayingFieldWipe08` | Triggers the tower-fall sound (a garbage-line sound in two-player) | `tetris.asm:5619-5645` | Source calls the placement a bug |
| `PlayingFieldWipe16` | Calls a display-refresh helper | `tetris.asm:5717` | — |
| `PlayingFieldWipe17` | Prints the score into the pause overlay tiles (`$9C6D`) | `tetris.asm:5724-5731` | Source *"Bug?"* — the pause line count updates only when pause is actually pressed |
| `PlayingFieldWipe18` | Prints the score at `$986D` | `tetris.asm:5740-5741` | — |
| `PlayingFieldWipe19` | Saves the counter to `$C0C7`, clears the counter, prints the line count (with a Type A / Type B split), runs Type B completion transitions, and prepares the next piece | `tetris.asm:5748-5758+` | Source pairs the next-piece call with a *"Bug?"* note at the dispatch site (`tetris.asm:214-215`) |

The port surfaces only the geometry and the counter→row mapping. **The wipe-sequence behavior above —
the row copy, the counter lifecycle, and these side effects — ports with the gameplay and presentation
code**, against this specification; it is not part of the data unit.

---

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_playing_field.py`, `--all`):
  `src/data/generated/playing_field_data.inc` (the four constants) and
  `tests/fixtures/playing_field_expected.h` (the 18 raw `(counter, vram, wram)` triples, independent
  of the port type). Regenerate after any upstream repin; do not hand-edit.
- **Hand-written port-design:** `src/data/playing_field.h` — the four constants included at namespace
  scope, and `playingFieldRowForWipeCounter`, the closed-form counter→row map. Header-only; there is
  no accessor body that earns a translation unit.

The addresses and the `$20` stride describe the DMG memory map, a mechanism the port does not
replicate, so they are pinned inside the parser and recorded in the fixture but do not appear in the
port surface. What the surface carries is the composition: the field's extent and the counter's
domain.

### Transcription asserts

`parse_playing_field.py` hard-errors (with a `file:line` citation) on any of: fewer or more than 18
`PlayingFieldWipe` routines, or numbers that are not the contiguous ascending run 02–19; a routine
missing the `ldh a, [hWipeCounter]` / `cp a, <n>` / `ret nz` gate, or whose compare operand does not
equal the label number; a routine missing exactly one `ld hl, $XXXX` then one `ld de, $XXXX` before
`call WipePlayingFieldRow`; an address pair that violates the closed form; `WipePlayingFieldRow`
missing its `ld b, <n>` row width or its counter increment; `PlayingFieldWipe19` missing the counter
reset; or a per-frame dispatch block that is not the 18 calls in descending 19 → 2 order. Documented
per-row extras (the side effects above, and `PlayingFieldWipe19`'s save-to-`$C0C7` between the gate
and the load) are tolerated as opaque code; the gate / load / call skeleton is mandatory.

---

## Tested by

`tests/test_playing_field.cpp` — the constants pinned against this contract; the full 18-row sweep of
the fixture against the closed form (both addresses, the lockstep stride, and contiguous counters);
the counter→row map checked against every fixture row and shown to decrease as the counter rises; and
the two ends of the walk pinned to concrete rows and addresses. The parser's own structural checks
(`tools/asm_parser/test_parse_playing_field.py`) guard the scan against upstream changes.
