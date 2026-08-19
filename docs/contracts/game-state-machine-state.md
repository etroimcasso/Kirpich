# Contract — Game-flow state (HRAM globals)

Reverse-derived behavioral contract for `GameFlowState` (`src/state/game_flow_state.h`): the block of
high RAM the original game keeps at `$FF80` and the main loop lives in, ported as one C++ struct. The
layout is transcribed from `tetris/hram.asm`; every address, use site, and adjudication below is from
`tetris/tetris.asm` (upstream `b95c668`). The line anchors are the authority the tests check against.

`GameFlowState` is an idiomatic surface, not a byte image. It carries only the bytes that are
game-flow state — the state-machine index, the frame and drop timers, the menu selections, the
piece-pipeline counters. The rest of the `$FF80` map belongs to other state surfaces (the sprite
renderer, the serial/multiplayer link, the demo recorder, the top scores) or to engine mechanism, or
is dead. Byte-level fidelity to the layout is held by the fixture (`tests/fixtures/hram_expected.h`)
and the widths pinned in `tests/test_game_flow_state.cpp`, not by the struct's memory image.

The `$FF80` map has a property the `$C000` map does not: **many live bytes have no label.** Seven
slots are commented out in `hram.asm` (`;hFF94::`, `;hFF98::`, …) yet are read and written every
frame, and several `ds` gaps conceal live state. The original reaches those bytes with a **raw
numeric operand** (`ldh [$98], a`) or a **pointer load** (`ld hl, $FFC6`) instead of a label, so a
label list alone cannot see them. The layout fixture is therefore paired with a **census** of every
such raw-operand access (below), and the boundary between this surface and the others is drawn **per
byte, not per label.**

---

## Field map

Every game-flow field, its HRAM label (or the raw address when the byte has no label), and the port
type. Addresses come from `hram.asm` via `tools/asm_parser/parse_hram.py`; the port type is the
hand-written shape in `game_flow_state.h`.

| Port field | ROM label | Address | Port type | Notes |
|---|---|---|---|---|
| `pieceLockStage` | *(unlabelled)* | `$FF98` | `uint8_t` | lock-delay stage; "Lockdown stage 3" `tetris.asm:5340` |
| `dropTimer` | `hDropTimer` | `$FF99` | `uint8_t` | frames until the piece steps down |
| `framesPerDrop` | `hFramesPerDrop` | `$FF9A` | `uint8_t` | gravity period for the level |
| `blinkCounter` | *(unlabelled)* | `$FF9C` | `uint8_t` | cursor / entry blink phase `tetris.asm:3976`, `5420`+ |
| `lines` | `hLines` | `$FF9E` | `uint16_t` | 2-byte packed-decimal in ROM; decimal port-side (below) |
| `completedRowCount` | *(unlabelled)* | `$FFA0` | `uint8_t` | rows completed by the current lock; read `tetris.asm:5343` |
| `timer1` | `hTimer1` | `$FFA6` | `uint8_t` | general frame timer, saturating auto-decrement (below) |
| `timer2` | `hTimer2` | `$FFA7` | `uint8_t` | second frame timer, decremented alongside `timer1` |
| `level` | `hLevel` | `$FFA9` | `uint8_t` | current level |
| `keyRepeatTimer` | `hKeyRepeatTimer` | `$FFAA` | `uint8_t` | DAS auto-repeat frame counter |
| `paused` | `hPaused` | `$FFAB` | `bool` | pause flag, domain `{0,1}` (below) |
| `nextPreviewPiece` | `hNextPreviewPiece` | `$FFAE` | `Piece` | the piece after the preview (invisible to the player) |
| `numPiecesPlayed` | `hNumPiecesPlayed` | `$FFB0` | `uint8_t` | "only incremented when determinism is required" |
| `gameType` | `hGameType` | `$FFC0` | `GameType` | boot 0 is "unset until the menu writes it" (below) |
| `musicType` | `hMusicType` | `$FFC1` | `MusicType` | boot 0 is "unset until the menu writes it" (below) |
| `typeALevel` | `hTypeALevel` | `$FFC2` | `uint8_t` | chosen Type A starting level |
| `typeBLevel` | `hTypeBLevel` | `$FFC3` | `uint8_t` | chosen Type B starting level |
| `typeBStartHeight` | `hTypeBStartHeight` | `$FFC4` | `uint8_t` | chosen Type B starting garbage height |
| `coarseCountdown` | *(unlabelled)* | `$FFC6` | `uint8_t` | counts `timer1` expiries; demo launch `tetris.asm:570`, blink cycles `2095`/`2252`. **Shared byte:** during top-score entry this is the name-entry column, carried as the high-score surface's `nameEntryColumn` (disjoint in time — see [`high-score-state.md`](high-score-state.md)) |
| `gameState` | `hGameState` | `$FFE1` | `GameState` | the dispatch index; boot value `NORMAL_GAMEPLAY` (below) |
| `frameCounter` | `hFrameCounter` | `$FFE2` | `uint8_t` | +1 every VBlank |
| `wipeCounter` | `hWipeCounter` | `$FFE3` | `uint8_t` | playing-field wipe animation step |
| `softDropCounter` | `hSoftDropCounter` | `$FFE5` | `uint8_t` | frames the piece has been soft-dropped |
| `rocketSpriteIndex` | `hRocketSpriteIndex` | `$FFF3` | `SpriteId` | rocket-tier sprite id, copied into a sprite object's id slot `tetris.asm:4964` |
| `heartMode` | `hHeartMode` | `$FFF4` | `uint8_t` | 0 = normal, non-zero = heart mode (below) |
| `tempPreviewPiece` | `hTempPreviewPiece` | `$FFFC` | `Piece` | shares the byte with the top-score pointer (below) |
| `topOutLockCount` | *(unlabelled, at `hTopScorePointerHi`)* | `$FFFB` | `uint8_t` | shares the byte with the top-score pointer (below) |

## `lines` is decimal

`hLines` is a two-byte packed-decimal (BCD) count: `daa` arithmetic, a `$9999` ceiling in Type A, a
down-count to zero in Type B (`tetris.asm:5347-5376`). The port stores a decimal `uint16_t` and keeps
BCD only on the wire, matching the score handling in `EngineState`. The ceiling and down-count
enforcement live with the line-clear code, not with this struct. The high byte's raw `$9F` accesses
and the `ld de, hLines + 1` form (`$FF9F`) are same-field accesses, not a separate field.

## Two bytes shared with the top-score pointer

Two HRAM bytes carry two independent purposes that never overlap **in time** — one during active
gameplay, the other only during top-score entry. The original overlays both purposes on one byte; the
port carries an independent field in each surface, and the split is safe because the uses are disjoint.

- **`$FFFC`** is labelled both `hTopScorePointerLo` and `hTempPreviewPiece` (the fixture records the
  alias). During score entry it is the low byte of the pointer to the score being entered (top-score
  surface); during the piece pipeline it is the staged preview piece. This struct carries
  `tempPreviewPiece`; the pointer belongs to the top-score surface.
- **`$FFFB`** is labelled `hTopScorePointerHi` (the pointer's high byte, top-score surface). But
  `tetris.asm:5268` reaches the same byte by its **raw address** — `ld hl, $FFFB` — and uses it as a
  persistent gameplay counter: "Counts number of pieces locked at the starting position … Top out when
  it hits 2." Each lock at the spawn position increments it (`inc [hl]`, `:5280`); on the second it
  forces game over (`ld a, $01` / `ldh [hGameState], a`, `:5273-5274`). This struct carries that
  counter as `topOutLockCount`; the pointer's high byte belongs to the top-score surface. The raw
  numeric access is why the byte needs its own field here rather than reading as a top-score byte.

## `paused` and `heartMode` widths — traced

Two flag bytes were sized by tracing their full value domain:

- **`paused` → `bool`.** `hPaused` is written `1` (`ld a, 1`, `tetris.asm:1772`), toggled with
  `xor a, 1` (`:4456`, `:4501` — a pure bit-0 flip, i.e. `paused = !paused`), and cleared with `xor a`
  (`:4545`). The domain is strictly `{0, 1}`, so it is a `bool`.
- **`heartMode` → `uint8_t`.** `hHeartMode` is written the **raw joypad-held byte** the frame DOWN is
  held on the title screen (`ldh a, [hJoyHeld]` / `bit PADB_DOWN, a` / `ldh [hHeartMode], a`,
  `tetris.asm:700-703`), and read only as zero / non-zero (`and a` / `jr z`, `:4169-4171`, also `:4028`,
  `:4060`, `:4243`). The non-zero value is not a single constant — it is whatever buttons were held — so
  the byte is a `uint8_t`, `0` = normal and any non-zero = heart mode.

## Typed fields and their boot values

`gameState` boots to `$00` = `NORMAL_GAMEPLAY`; the copyright-screen init writes `$24` before the first
dispatch (a runtime detail, not a lifecycle value). `gameType` and `musicType` have no valid `0`
enumerator (`GameType` is `$37`/`$77`, `MusicType` is `$1C`–`$1F`); their boot value is "unset until the
menu writes it," exactly as in ROM, so the tests pin the boot byte as `0` but do not assert enumerator
validity. `nextPreviewPiece`, `tempPreviewPiece`, and `rocketSpriteIndex` boot to their zero value and
are overwritten before use.

---

## The raw-operand census

`parse_hram.py` scans `tetris.asm` for every **static raw-operand HRAM access** and emits a
`{address, refCount}` table (`kHramCensus`). In scope: numeric `ldh` operands (`[$98]`, `[$FFD1]`,
`[$86 + 6]`) and 16-bit pointer loads of an HRAM address (`ld hl, $FFC6`, `ld de, hLines + 1`,
`ld hl, hLevel`). Out of scope, by design: symbolic `ldh [hLabel]` accesses (the label is already in
the layout table); hardware registers (`$FF00`–`$FF7F`) and `rIE` (`$FFFF`); and dynamic flows — the
`ldh [c], a` C-register form (`tetris.asm:361`) and the `inc l` timer walk that reaches `hTimer2`
without a static operand. The census covers 57 addresses across 244 access sites.

The census is what makes the per-byte boundary testable: `tests/test_game_flow_state.cpp` resolves
**every** census address to exactly one owner — a game-flow byte, a labelled byte owned by a later
surface, an unlabelled gap byte the map assigns to a later surface, an engine-mechanism byte, or a
dead byte. A census address matching none fails the test.

### Ownership of the whole `$FF80` map

Per-byte ownership of `$FF80`–`$FFFE`. "Game-flow" is this contract; the other surfaces are separate
state types that reuse this same layout fixture at their own time.

| Address(es) | ROM label / region | Owner |
|---|---|---|
| `$FF80`–`$FF81` | `hJoyHeld`, `hJoyPressed` | engine mechanism — per-tick input snapshot, delivered by the engine input bridge |
| `$FF82`–`$FF84` | gap | unused (no access) |
| `$FF85` | `hVBlankInterruptTriggered` | engine mechanism — VBlank latch the engine loop replaces |
| `$FF86`–`$FF97` | sprite-renderer gap + `hSpriteRenderer*` (incl. gap `$FF94`) | sprite-renderer state |
| `$FF98` | *(unlabelled)* | **game-flow** — `pieceLockStage` |
| `$FF99`–`$FF9A` | `hDropTimer`, `hFramesPerDrop` | **game-flow** |
| `$FF9B` | gap | engine mechanism — init-zeroed, sprite-path write "never checked?" |
| `$FF9C` | *(unlabelled)* | **game-flow** — `blinkCounter` |
| `$FF9D` | gap | unused |
| `$FF9E`–`$FF9F` | `hLines` (dw) | **game-flow** — `lines` (both bytes) |
| `$FFA0` | *(unlabelled)* | **game-flow** — `completedRowCount` |
| `$FFA1` | `hSavedIE` | engine mechanism — `rIE` shadow inside `DisableLCD` (`tetris.asm:6461`) |
| `$FFA2`–`$FFA3`, `$FFA5` | gap | unused |
| `$FFA4` | gap | dead — one write, "Unused?" |
| `$FFA6`–`$FFA7` | `hTimer1`, `hTimer2` | **game-flow** |
| `$FFA8` | *(unlabelled)* | unused |
| `$FFA9`–`$FFAB` | `hLevel`, `hKeyRepeatTimer`, `hPaused` | **game-flow** |
| `$FFAC`–`$FFAD` | `hMarioStartHeight`, `hLuigiStartHeight` | serial/multiplayer state — per-player start heights |
| `$FFAE` | `hNextPreviewPiece` | **game-flow** |
| `$FFAF` | gap | dead — "Piece List Pointer high byte, but never used?" |
| `$FFB0` | `hNumPiecesPlayed` | **game-flow** |
| `$FFB1` | gap | serial/multiplayer state |
| `$FFB2`–`$FFB5` | gap | sprite-renderer state |
| `$FFB6`–`$FFBF` | `hDMARoutine` (ds `$A`) | engine mechanism — copied DMA code bytes, not state |
| `$FFC0`–`$FFC4` | `hGameType`, `hMusicType`, `hTypeALevel`, `hTypeBLevel`, `hTypeBStartHeight` | **game-flow** |
| `$FFC5` | `hIsMultiplayer` | serial/multiplayer state |
| `$FFC6` | *(unlabelled)* | **game-flow** — `coarseCountdown` |
| `$FFC7` | `hNewTopScore` | top-score state |
| `$FFC8`–`$FFCA` | gap | top-score state |
| `$FFCB`–`$FFD6` | `hSerial*` + gaps (`$FFCE`, `$FFD1`–`$FFD6`) | serial/multiplayer state |
| `$FFD7`–`$FFD8` | `hOurWins`, `hTheirWins` | serial/multiplayer state |
| `$FFD9`–`$FFDC` | gap | serial/multiplayer state |
| `$FFDD`–`$FFDF` | gap | unused |
| `$FFE0` | *(unlabelled)* | **game-flow** — `scorePrintFlag` (`tetris.asm:188`/`245`/`6618`; see [`readouts.md`](readouts.md) §3) |
| `$FFE1`–`$FFE3` | `hGameState`, `hFrameCounter`, `hWipeCounter` | **game-flow** |
| `$FFE4` | `hDemoNumber` | demo-recording state |
| `$FFE5` | `hSoftDropCounter` | **game-flow** |
| `$FFE6` | `hLevelTilemapPointerLo` | engine mechanism — level-select VRAM draw scratch (`tetris.asm:4151`/`4163`) |
| `$FFE7` | gap | unused |
| `$FFE8` | `hRedrawTopScoresDuringVBlank` | top-score state |
| `$FFE9`–`$FFEE` | `hDemoRecording`, `hDemoJoypad*`, `hSavedJoyHeld` | demo-recording state |
| `$FFEF`–`$FFF0` | gap | serial/multiplayer state |
| `$FFF1`–`$FFF2` | `hSavedSerialTx`, `hSavedSerialRx` | serial/multiplayer state |
| `$FFF3`–`$FFF4` | `hRocketSpriteIndex`, `hHeartMode` | **game-flow** |
| `$FFF5`–`$FFFA` | gap | unused |
| `$FFFB` | `hTopScorePointerHi` | top-score state **and game-flow** `topOutLockCount` (shared byte, above) |
| `$FFFC` | `hTopScorePointerLo` / `hTempPreviewPiece` | top-score state **and game-flow** `tempPreviewPiece` (shared byte, above) |
| `$FFFD` | gap | unused |
| `$FFFE` | *(below the section end)* | engine mechanism — boot HRAM-clear loop top (`ld hl, $FFFE`, `tetris.asm:347`) |

The unused rows carry no census access; the map records them for completeness. `hSavedJoyHeld`
(`$FFEE`) is grouped with the demo recorder pending that surface's own port; it is reached only
symbolically, so it is not a census row and the grouping is provisional.

## Boot and decrement semantics

- **Boot is all-zero.** The startup clear loop wipes `$FFFE` downward through all of HRAM
  (`tetris.asm:347-352`, the `ld b, $80` off-by-one included), so a default-constructed `GameFlowState`
  — every member zero — is the boot state, and `reset()` returns a live instance to it. Per-field
  game-start initialisation (choosing a level, seeding the drop timing) is the job of the systems that
  own those fields, not of this struct.
- **`timer1`/`timer2` auto-decrement.** The main loop decrements both once per pass, saturating at
  zero; the loop reaches `hTimer2` via `inc l` after `hTimer1`, a dynamic access the census does not
  see. The decrement mechanism ports with the main loop; only the fields and their semantics are here.
- **`gameState` dispatch.** Every frame the main loop reads `gameState` and jumps through the handler
  table (`tetris.asm:386-417`); a handler may write a new value to transition. The dispatch mechanism
  is main-loop work; the field is here.

---

## The surface

- **Parser-emitted** (`tools/asm_parser/parse_hram.py`, `--all`): `tests/fixtures/hram_expected.h` —
  the `{name, address, size}` layout row for every label plus the anonymous gaps, and the
  `{address, refCount}` census table. Layout + census only; there are no ROM data values in this unit.
  Regenerate after any upstream repin; do not hand-edit.
- **Hand-written port-design:** `src/state/game_flow_state.h` (the `GameFlowState` struct and
  `reset()`), the tests, and this contract.

### Transcription asserts

`parse_hram.py` hard-errors (with a `file:line` citation) on any of: a `ds $HIGH − $LOW` gap whose
`$LOW` is not the running address; an unrecognised directive; a region of non-positive size or outside
`$FF80`–`$FFFD`; a section that does not tile exactly; a walk that does not end at `$FFFE`; or an `ldh`
operand form the census scanner does not recognise (never a silent skip). The walk derives every
address from the `$FF80` origin and the reservations that precede each label — no address is
hand-typed.

---

## Tested by

`tests/test_game_flow_state.cpp` — the layout-tiling sweep (every `hram.asm` region parses, addresses
strictly ascending, sizes positive, the section tiled `$FF80`–`$FFFE` with no overlap or hole, exactly
one alias at `$FFFC`); the width pins tying every labelled game-flow field to the struct (`hLines` two
bytes ↔ `uint16_t lines`; the `Piece`/enum members one byte each); the **census boundary guard** (every
raw-operand address resolves to exactly one owner); the reset-to-boot behavioural test (mutate every
member, `reset()`, compare against a fresh instance); the typed-member boot pins (`gameState` ==
`NORMAL_GAMEPLAY`, the `gameType`/`musicType` "unset" boot bytes, the shared-byte split); and the
unlabelled-fold-in census pins (`$FF98`/`$FF9C`/`$FFA0`/`$FFC6`/`$FFE0` each raw-accessed and
genuinely label-less). The parser's own checks (`tools/asm_parser/test_parse_hram.py`) guard the walk and the
census against upstream changes.
