# High-score state — behavioral contract

The reverse-derived contract for the top-score surface: the two work-RAM tables the game keeps its
high scores in, the high-RAM bytes the score-entry flow reads and writes, the layout of a table
slice, the insert / name-entry / display mechanisms (deferred to later game-flow work, recorded here), and the
port's persistence format. Anchors are `tetris.asm` line numbers unless noted; the upstream is pinned
at `b95c668`.

This surface is `src/state/high_score_state.h` (`HighScoreState` + `TopScoreEntry`) and its
persistence pair `src/state/high_score_persistence.{h,cpp}`. It consumes the existing WRAM and HRAM
layout fixtures (`tests/fixtures/wram_expected.h`, `hram_expected.h`) with no parser change.

## The two tables

`wram.asm:56-60` declares them adjacently at the top of the upper WRAM bank:

```
wTypeBTopScores:: ds 10*6*3*(6+3)   ; 10 levels, 6 starting heights, 3 entries, 6 name + 3 score bytes
wTypeATopScores:: ds 10*3*(6+3)
```

| Table | Address | Bytes | Dimensions |
|---|---|---|---|
| `wTypeBTopScores` | `$D000` | 1620 | 10 levels × 6 heights × 3 ranks × 9 |
| `wTypeATopScores` | `$D654` | 270 | 10 levels × 3 ranks × 9 |

Type A begins exactly where Type B ends (`$D654 == $D000 + 1620`). The port models them as nested
arrays: `typeB[level][height][rank]`, `typeA[level][rank]`, each cell a `TopScoreEntry`.

### Slice layout (27 bytes)

A slice is one (level[, height]) group of three ranked entries. The scores come first as a contiguous
9-byte block, then the names as a contiguous 18-byte block — **grouped, not interleaved**:

```
offset 0   3   6   9        15        21        27
       | s0| s1| s2|  name0  |  name1  |  name2  |
        \__ 3 scores __/ \______ 3 names (6 bytes each) ______/
```

- **Score** — 3 packed-decimal (BCD) bytes, **low pair first** (byte +0 = the two least significant
  digits, byte +2 = the two most significant). Ceiling 999999. The slice walk hands `UpdateTopScores`
  the pointer at +2 (the high pair) and compares high-pair-first (`:3744-3762`); ranks are reached by
  `inc de` ×3, which is what proves the three scores are contiguous.
- **Name** — 6 charmap glyph bytes, first glyph at the lowest address, `$00`-delimited when the name
  is shorter than six. The seed name is `"a"` followed by five `"…"` (`:3814-3823`).

Rank 0 is the best score (displayed first), rank 2 the worst.

The slice walk: `UpdateTypeATopScores` (`:3641-3659`) strides `wTypeATopScores` by `3*(6+3)`=27 per
level; `UpdateTypeBTopScores` (`:3661-3689`) strides `wTypeBTopScores` by `3*6*(6+3)`=162 per level
then `$001B`=27 per starting height. Both `inc hl` twice to reach the first score's high pair, then
call `UpdateTopScores`.

## Owned HRAM bytes

| Port field | Upstream | Address | Type | Notes |
|---|---|---|---|---|
| `newTopScore` | `hNewTopScore` | `$FFC7` | `bool` | set `:3833`, cleared `:534`/`:4009`; domain {0,1} |
| `topScoresRedrawRequested` | `hRedrawTopScoresDuringVBlank` | `$FFE8` | `bool` | set `:3889`, consumed+cleared by VBlank `:3931`; domain {0,1} |
| `newScoreRank` | *(unlabelled, census)* | `$FFC8` | `uint8_t` | rank of the new score; **inverted: 3 = 1st, 2 = 2nd, 1 = 3rd** |
| `nameEntryColumn` | *(unlabelled, shared)* | `$FFC6` | `uint8_t` | name-entry cursor column 0..5; **shared byte** (below) |

`newScoreRank` is written `:3797-3798` (`ld a, c` / `ldh [$C8], a`) where `c` is the compare loop's
countdown from 3: it holds 3 while checking against the best score, so beating the best records a 3.
The wire value is kept verbatim; the inversion is the original's (the same posture as `ActiveDemo`'s
demo-number inversion in the demo state).

### `$FFC6` is a shared byte (name-entry column)

`$FFC6` carries `GameFlowState::coarseCountdown` during gameplay (timer1-expiry counter — demo launch,
victory/defeat blink cycles) and the top-score **name-entry column** during score entry. The two uses
are disjoint in time. `UpdateTopScores` zeroes it at insert (`:3830`, `ldh [$C6], a`); `GameState_15`
inc/decrements it as the cursor moves across the six columns (`:4084-4110`). This is the same overlay
split the game-flow contract records for `$FFFB` (`topOutLockCount`) and `$FFFC` (`tempPreviewPiece`):
per-byte ownership stays with the game-flow surface (its census guard's `$FFC6` row is unchanged); the
high-score surface carries `nameEntryColumn` as an overlay field. Adjudicated 2026-08-14
(user-approved).

### The name-cursor pointer `$FFC9`/`$FFCA` gets no field

`UpdateTopScores` stores the address of the name cell being entered big-endian across `$FFC9`/`$FFCA`
(`:3824-3827`, `ld a, d` / `ldh [$C9], a` / `ld a, e` / `ldh [$CA], a` — the same "Bug?" big-endian
storage the pointer-hi/lo pair uses). This is fully derivable state: the pointer = the name field of
(`gameType` table, level[, height], `newScoreRank`, `nameEntryColumn`). The port recomputes it rather
than storing it. `GameState_15` reads it back (`:3967-3970`) and re-derives it as the column moves
(`:4095-4099`). The census (refCounts 6/7) covers this and a second, disjoint role — `GameState_2B` /
`_2C` reuse the pair as a VRAM typewriter cursor for the Type-B ending message (`:2851-2854`,
`:2880-2906`), game-flow machinery out of this surface. The surface's verdict for both bytes is
**mechanism, no field**.

### The `$FFFB`/`$FFFC` pointer role: mechanism

`UpdateTopScores` writes the slice pointer to `hTopScorePointerHi`/`Lo` (`$FFFB`/`$FFFC`) on entry
(`:3738-3741`) and reads it back within the same call (`:3767-3770`, `:3837-3840`); it never persists
across frames. The persistent roles at those addresses already live in the game-flow surface
(`topOutLockCount` at `$FFFB`, `tempPreviewPiece` at `$FFFC`), reached by raw address. The three lone
`hTopScorePointerHi` zero writes (`:531`, `:1224`, `:4131`) are that counter's resets, not pointer
writes. The game-flow contract's "the pointer belongs to the top-score surface" hand-off is discharged
here: the surface's adjudication of those two bytes is **mechanism** — no high-score field.

## Mechanisms (deferred to later game-flow work, recorded here)

Re-implemented against this struct when the menu / game-over / name-entry systems are built:

- **Slice walk** — `UpdateTypeATopScores` / `UpdateTypeBTopScores` locate the (level[, height]) slice.
- **Insert** — `UpdateTopScores` (`:3737-3833`): compares `wScore` high-pair-first against the three
  entries (with the `dec l` page "Bug?" at `:3752`), shifts lower entries down with the backwards
  `CopyThreeBytes` (`:3726-3735`, decrements DE; reused at `b=6` for names via `.copyBBytes`), seeds
  the fresh name `"a"` + five `"…"`, records the rank in `$FFC8`, stores the name-cursor pointer in
  `$FFC9`/`$FFCA`, zeroes the blink byte `$FF9C` and the column `$FFC6`, cues music `$01`
  (`wNewMusicID`), and sets `hNewTopScore`.
- **Display** — staging rows at `$C9A4`/`$C9AC` (+`$20` stride, inside the `$C800–$CBFF` board shadow
  window) filled by `ClearTopScoreFields` (`:3934-3950`, `"…"`×14 per column) and the print loops
  (`:3835-3890`); `PrintTopScore` (`:3694-3722`) **skips leading-zero tiles** rather than printing
  spaces — the "Bug" comment and the visible leading-`"…"` quirk. `DrawTopScoresToVRAM` (`:3893-3932`,
  called at `:235`) flushes the staging rows to VRAM on `hRedrawTopScoresDuringVBlank` and clears it.
- **Name entry** — `GameState_15` (`:3952-4112`): `$FFC8` picks the VRAM row, `$FFC6` the column,
  `$FFC9`/`$FFCA` the WRAM cell; blink via `$FF9C` (the game-flow `blinkCounter`); the letter wheel runs
  `"a"`→…→`"×"` (heart mode swaps in `"♥"`)→`" "`→`"a"` (`:4025-4077`); A advances / submits past
  column 6, B retreats, START submits (`:4004-4017`, clears `hNewTopScore`, routes to state `$11`
  Type A / `$13` Type B by `hGameType`). Key repeat via the game-flow `keyRepeatTimer`.

## Quirks — preserved verbatim, never "fixed"

- **Rank inversion** — 3 = 1st place. The wire counter runs downward from 3.
- **Big-endian pointer store** — `$FFC9`/`$FFCA` (and `$FFFB`/`$FFFC`) store a pointer high-byte-first
  on a little-endian CPU (the "Bug?" comments).
- **Leading-zero display** — `PrintTopScore` skips over leading zero tiles instead of blanking them, so
  the `"…"`-filled field shows through.
- **`dec l` page quirk** — `:3752` decrements `l` where `dec hl` was likely intended ("Bug? Could this
  ever be the case?"); harmless for the aligned tables.
- **Backwards copies** — `CopyThreeBytes` decrements DE, so shifts run end-first.

## Boot and persistence

- **Boot** — a default-constructed `HighScoreState` is the hard-boot state. `Init::` (`:264-274`)
  zeroes `$D000`–`$DFFF` (the whole upper WRAM bank), so a power cycle clears the tables; a zero score
  prints as all-skipped digits and a zero name terminates immediately — the genuine post-boot display.
  The `$DF00`–`$DFFF` double-clear (`:311-317`, "clears … a second time") is a noted bug, no port
  artifact.
- **Soft-reset survival** — the `Init::.softReset` entry (`:276`) deliberately skips the `$D000` clear
  ("so soft-resetting doesn't erase the top scores?"), so top scores survive a soft reset and nothing
  else. This is the original's only persistence.
- **Port persistence (enhancement)** — the port persists the tables across launches through the
  engine's `retropp::SaveStore` (named, schema-versioned, atomic byte documents in a per-user
  directory), always on, no toggle (user-ordered 2026-08-14). This strictly extends the soft-reset
  survival; the in-sim table behaviour is byte-faithful and unchanged. Format:
  - **Document** `"topscores"`, schema version **1**, save identity `Kirpich` / `Kirpich`.
  - **Payload** — the exact ROM wire image, **1890 bytes**: the `wTypeBTopScores` image (1620) then
    the `wTypeATopScores` image (270), in address order, byte-for-byte the WRAM layout above. Only the
    tables persist; the HRAM session fields (`newTopScore`, rank, column, redraw) never do.
  - **Load policy** — absent document → boot zeros (first run); present → decode; corrupt
    (`SaveStoreError`) or wrong length → log, run with boot zeros, **leave the damaged file in place**
    (never treated as absent, never proactively overwritten).
  - Wiring the calls into the game loop (load at startup, save on name-entry submit) is later
    game-flow work; nothing can earn a score before the loop exists.
