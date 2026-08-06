# Gravity

How fast pieces fall. A drop timer counts down once per frame; at zero the piece falls one cell and
the timer reloads from a 21-entry table indexed by the current level. This is the first ported table
with a gameplay-math consumer rather than a rendering one — the drop-timing loop reads it on every
piece spawn and every gravity drop — so it ships with the lookup, not just the values.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `FramesPerDropEntry` | `src/data/gravity.h` | 2-byte struct, `{ level, frames }` |
| `kFramesPerDrop` | `src/data/gravity.h` | `std::array<FramesPerDropEntry, 21>` |
| `kMaxLevel` | `src/data/gravity.h` | `uint8_t` = 20 |
| `kHeartModeLevelBoost` | `src/data/gravity.h` | `uint8_t` = 10 |
| `framesPerDrop(level, heartMode)` | `src/data/gravity.{h,cpp}` | `uint8_t` |

The intervals run 52 frames at level 0 down to 2 at level 20, non-increasing, with the sharpest step
at level 9 → 10. The exact values and their sources are pinned in
[`../contracts/gravity.md`](../contracts/gravity.md).

## Decisions

**The level is a stored field, not the array position alone.** A row carries the `level` it applies
to as well as its `frames`, and a `static_assert` holds every row at the position its level names.
The table is indexed directly by level, so the identity is load-bearing; storing it makes a
transcription slip a compile error instead of a silent off-by-one.

**The lookup ports the original's arithmetic exactly, including where it does *not* check.** Heart
mode shifts the index up 10 levels and caps it at 20; normal mode applies no cap at all, because the
original's cap instructions sit inside the heart-mode branch. Mirroring the cap onto both paths
would be tidier and wrong — no reachable input distinguishes them today, but the port's job is to
match the original's shape, not to improve it.

**Out-of-range levels assert rather than clamp.** Level 21 and above cannot occur: the menu offers
0–9, a two-player game forces 1, and levelling up stops at 20. The original has no bounds check
because it needs none. Inventing a clamp would define behavior the original never defines and would
quietly absorb a caller bug; a debug assert surfaces it instead.

**Heart mode enters as a `bool`.** Upstream stores the raw held-buttons byte in the flag, so its
truth is "nonzero", and every reader tests it that way. The port resolves that encoding at the
boundary and passes a `bool` inward rather than carrying a joypad byte into the data layer.

**The lookup returns; it does not store.** The original writes its result into both the drop
countdown and the countdown's reload value. Those are gameplay-loop state, so this returns the byte
and the caller stores it — which also keeps the data layer free of mutable state.

**It has a `.cpp`.** Unlike the sprite grids, this unit has real behavior (a shift, a cap, a
precondition), so the body lives in a translation unit rather than the header.

## Keeping it honest

The table and its test fixture are generated from the disassembly by
`tools/asm_parser/parse_gravity.py`, which checks the source's structure as it reads — the label
appears once, every line inside is a single-value decimal `db`, the count is 21, and rows 0, 10 and
20 carry the level annotations the original writes beside them — and stops with a citation rather
than emitting a wrong file. Those annotations are what catch a shifted row. The fixture holds the
values as raw bytes with no port type in it, so the test sweep compares the typed table against
something independent of it rather than against itself. See
[`../engine/gravity.md`](../engine/gravity.md) for how to regenerate and change it.

## Not here yet

The drop timer itself — the per-frame countdown, the reload on spawn and after each drop, and the
Type B quirk that overwrites the first countdown with the level-0 interval — is gameplay-loop
behavior and ports with that loop. The contract records it so the loop has a specification to build
against.
