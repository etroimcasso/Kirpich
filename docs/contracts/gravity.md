# Contract — Gravity

Reverse-derived behavioral contract for Kirpich's gravity table: how long a piece waits between
automatic drops at each level, and the lookup that picks the wait. Every value here is transcribed
from the `kaspermeerts/tetris` disassembly (upstream `b95c668`); the line anchors below are the
authority the tests check against.

Gravity is a countdown. A drop timer ticks down once per frame; at zero the piece falls one cell and
the timer reloads from this table. Level is the only input — plus one hidden modifier, heart mode.

---

## The table

`FramesPerDropTable::` (`tetris.asm:4262`) — 21 decimal `db` rows, one per level, terminating at
`InitDemoGarbage::` (`tetris.asm:4286`). The array position **is** the level: the lookup indexes the
table directly with no offset, so row N is the drop interval at level N. Upstream annotates rows 0,
10 and 20 with their level, and those comments pin the row positions against the source.

| Level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **Frames** | 52 | 48 | 44 | 40 | 36 | 32 | 27 | 21 | 16 | 10 | 9 |

| Level | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Frames** | 8 | 7 | 6 | 5 | 5 | 4 | 4 | 3 | 3 | 2 |

The sequence is **non-increasing**, never flat for more than two consecutive levels, and its sharpest
step is level 9 → 10, where the wait drops from 10 frames to 9 after falling by 5 or 6 at every step
before it. At the DMG's ~59.73 Hz that is roughly 0.87 s per cell at level 0 and 0.03 s at level 20.

Boundary values (hand-traced against `tetris.asm:4262-4283`):

| Entry | Frames | Anchor |
|---|---|---|
| `kFramesPerDrop[0]` | 52 | `tetris.asm:4263` — `; Level 0` |
| `kFramesPerDrop[5]` | 32 | `tetris.asm:4268` |
| `kFramesPerDrop[9]` | 10 | `tetris.asm:4272` — above the cliff |
| `kFramesPerDrop[10]` | 9 | `tetris.asm:4273` — `; Level 10`, below the cliff |
| `kFramesPerDrop[15]` | 5 | `tetris.asm:4278` |
| `kFramesPerDrop[20]` | 2 | `tetris.asm:4283` — `; Level 20` |

## The lookup

`LookupGravity::` (`tetris.asm:4240-4260`). Reads the current level, applies the heart-mode shift if
heart mode is armed, reads the table, and stores the result.

```
ldh a, [hLevel]          ; index = level
ld e, a
ldh a, [hHeartMode]
and a
jr z, .lookup            ; heart mode off: index is the level, unmodified
ld a, 10
add e                    ; heart mode on: index = level + 10
cp a, 21
jr c, .store             ; ... passed through when it is 20 or less
ld a, 20                 ; Gravity tops out at level 20
.store
ld e, a
.lookup
ld hl, FramesPerDropTable
...
```

**Normal mode.** The index is the level, used unchanged. There is no bounds check — see *Level
bounds* below for why the game cannot produce an out-of-range level.

**Heart mode.** The index is `level + 10`, capped at 20. The comparison is `cp 21`, so a boosted
index of exactly 20 **passes through uncapped** and only 21 and above become 20 — the two cases are
indistinguishable in the result (both index row 20) but the boundary matters to anyone re-deriving
the arithmetic. In practice: levels 0–10 shift by a full 10 levels, levels 11–20 all land on the
level-20 interval of 2 frames.

| Level | 0 | … | 9 | 10 | 11 | … | 20 |
|---|---|---|---|---|---|---|---|
| **Heart-mode frames** | 9 | … | 3 | 2 | 2 | … | 2 |

**Only heart mode caps.** The cap instructions sit inside the heart-mode branch; the normal path
reaches the table read with no comparison at all. Porting the cap to both paths would be wrong even
though no reachable input distinguishes them.

**The store is the caller's.** `LookupGravity` writes the byte into *both* the drop countdown
(`hDropTimer`, `tetris.asm:4258`) and its reload value (`hFramesPerDrop`, `tetris.asm:4259`) — the
current wait and the wait for every later drop, seeded together. The port's `framesPerDrop()`
**returns** the byte and writes nothing; that dual store is countdown state and ports with the
gameplay loop that owns it.

### Level bounds

The port's lookup requires `level <= 20` and asserts it in debug builds. The original has no such
check because no code path can produce a higher level. Every writer of the level:

| Writer | Anchor | Value |
|---|---|---|
| Game start | `tetris.asm:4153` | copied from the menu selection (`hTypeALevel` / `hTypeBLevel`) |
| Two-player start | `tetris.asm:1241` | forced to 1 — upstream comments this `; Why? Bug` |
| Level up | `tetris.asm:5852` | `inc`, gated at 20 |

The menu selection is a 2×5 grid of levels 0–9 (`tetris.asm:3371` — *"Levels 0-4 make up the top
row"*), so a game starts no higher than 9. The level-up path reads the level and returns early on
`cp a, $14 / ret z` (`tetris.asm:5834-5835`, `$14` = 20) *before* the increment at `:5852`, so 20 is
terminal. Level 21 is unreachable, and the port declines to define behavior for it rather than
inventing a result the original never produces.

### Heart mode

A hidden difficulty modifier, stored at `hHeartMode` (`hram.asm:216`, commented *"Heart mode. Get
it?"*). Armed at the title menu by holding **Down** while pressing Start (`tetris.asm:698-703`):

```
ldh a, [hJoyHeld]
bit PADB_DOWN, a
jr z, .normalMode
ldh [hHeartMode], a      ; stores the raw joypad byte, not a 1
```

The stored value is the whole held-buttons byte, so the flag's truth is **nonzero = on**, not
`== 1` — and every reader tests it with `and a` / `jr z`. The port takes it as a `bool`, resolving
the encoding at the boundary rather than carrying the joypad byte inward. When it is on, the level
readout draws a `"♥"` beside the level number (`tetris.asm:4169-4175`); the displayed level itself is
unchanged, which is the point — the game runs 10 levels faster than it claims to.

## Consumers

`LookupGravity` is called from three places, all of them "the level just changed, reseed the drop
timer": game start (`tetris.asm:4196`), two-player start (`tetris.asm:1263`), and level-up
(`tetris.asm:5875`).

Downstream, the reload value feeds the drop timer at piece spawn (`tetris.asm:5162-5163`) and after
every gravity drop (`tetris.asm:5218-5219`); the per-frame countdown itself is
`tetris.asm:5202-5206`. Those port with the gameplay loop, not here.

**Type B quirk.** Immediately after the game-start call, a Type B game overwrites the drop
*countdown* with `$34` (52 — the level-0 interval) while leaving the reload value at the level's own
interval (`tetris.asm:4212-4213`). The first piece of a Type B game therefore falls at level-0 speed
no matter what level was chosen; every piece after it uses the real interval. This is caller
behavior and ports with the game-start path.

---

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_gravity.py`, `--all`):
  `src/data/generated/gravity_data.inc` (the 21 rows) and `tests/fixtures/gravity_expected.h` (the
  same 21 values as raw bytes, independent of the port type). Regenerate after any upstream repin; do
  not hand-edit.
- **Hand-written port-design:** `src/data/gravity.h` (the `FramesPerDropEntry` type, the level
  constants, the order `static_assert`) and `src/data/gravity.cpp` (the lookup body).

### Transcription asserts

`parse_gravity.py` hard-errors (with a `file:line` citation) on any of: `FramesPerDropTable::`
missing or defined more than once; a line inside the table that is not a single-value `db`, blank, or
comment; a row count other than 21; a value that is not a decimal byte; a missing terminating label;
or a row 0 / 10 / 20 whose `; Level N` comment anchor is absent or names a different level. The
anchors are what catch a shifted row: dropping any row ahead of one slides every later row and the
anchor no longer lands where the source says it should.

---

## Tested by

`tests/test_gravity.cpp` — the full 21-row sweep of the table against the fixture, the boundary
values above, and both lookup modes across their whole domain (levels 0–20 in normal mode, and 0–20
in heart mode pinning the shift and the cap level by level). The parser's own structural checks
(`tools/asm_parser/test_parse_gravity.py`) guard the scan against upstream changes.
