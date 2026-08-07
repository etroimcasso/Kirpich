# Playing field

The board pieces fall onto: 18 rows by 10 columns. This unit ports two things about it — its fixed
extent, and the schedule that redraws it a row at a time after the stack changes. It is geometry and
a mapping, not gameplay: nothing here moves a piece or clears a line.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `kPlayingFieldRows` | `src/data/playing_field.h` | `uint8_t` = 18 |
| `kPlayingFieldCols` | `src/data/playing_field.h` | `uint8_t` = 10 |
| `kPlayingFieldWipeCounterFirst` | `src/data/playing_field.h` | `uint8_t` = 2 |
| `kPlayingFieldWipeCounterLast` | `src/data/playing_field.h` | `uint8_t` = 19 |
| `playingFieldRowForWipeCounter(counter)` | `src/data/playing_field.h` | `uint8_t` |

After a line clear compacts the stack — and again for the game-over curtain — the field is copied
back to the screen one row per frame, from the bottom up. The original drives that with a counter it
steps from 2 (the bottom row) to 19 (the top). `playingFieldRowForWipeCounter` maps a counter value
to the row it redraws (0 = top, 17 = bottom). The exact addresses, the counter's full lifecycle, and
the per-row side effects are pinned in [`../contracts/playing-field.md`](../contracts/playing-field.md).

## Decisions

**The wipe is a mapping, not 18 stored patterns.** It is tempting to expect a table of 18
coordinate lists. There is none: the original holds 18 near-identical routines, each copying one
10-byte row between two addresses that are exactly `base + (19 − counter) × 32`. So there is no data
to store beyond the field's extent and the counter's range; the whole schedule collapses to one
closed-form line, `playingFieldRowForWipeCounter`.

**The addresses stay out of the port surface.** The routines' source and destination addresses are
the Game Boy's memory map — the background-map location of the field and the work-RAM shadow copy —
which the port does not reproduce. They are checked by the generator and recorded in the test fixture,
but the shipped surface carries only what survives the platform change: how big the field is, and
which row a counter value redraws.

**No struct, no translation unit.** Nothing here is a multi-field record, and the one function is a
single subtraction, so the whole unit is four constants and one `constexpr` in a header. There is no
`.cpp`.

**Out-of-range counters are undefined, not clamped.** The mapping is meaningful only for counters 2
through 19; outside that range the original never reaches a row copy, so the port defines no answer
there rather than inventing one. The precondition is documented on the function.

## Keeping it honest

The four constants and the test fixture are generated from the disassembly by
`tools/asm_parser/parse_playing_field.py`, which reads the 18 wipe routines by their shape — the
counter gate, the two address loads, the shared row-copy call — and stops with a citation if any of
them has moved, rather than emitting a wrong file. It checks that the routines are the contiguous run
02–19, that every address obeys the closed form, that the row-copy helper still loads a width of 10
and increments the counter, and that the per-frame dispatch still calls all 18 in descending order.
The fixture holds the raw addresses with no port type in it, so the test sweep compares the closed
form against source values rather than against itself. See
[`../engine/playing-field.md`](../engine/playing-field.md) for how to regenerate it.

## Not here yet

The wipe *behavior* — the row copy, the counter's lifecycle, and the side effects a few particular
rows trigger (a sound on one row, score and line-count printing on others) — is gameplay and
presentation code and ports with those loops. The contract records all of it so those loops have a
specification to build against; this unit gives them the geometry and the row mapping to build on.
