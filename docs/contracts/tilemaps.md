# Background tilemaps — behavioral contract

Reverse-derived from `kaspermeerts/tetris` (`tetris.asm`, `charmap.asm`), pinned upstream. This
document is the authority the tilemap tests are written against. Line anchors (`tetris.asm:NNNN`)
cite the pinned commit.

The port carries the tilemap **data** — the tile-index grids of every static screen. Where each
screen is drawn, which cells the game later overwrites, and the timing of the printed screens are
**consumer behavior**: recorded here as context so the data's shape is justified, but drawn by the
screen and presentation layers, not by this unit.

## What a tilemap is

A background tilemap is a rectangle of **tile indices** — one byte per background cell, each byte
naming a tile in the loaded tile sheet. The original stores each screen as a run of `db` lines,
sometimes as raw `$HH` bytes, sometimes as `db "text"` rows the assembler resolves through the
character map, sometimes both on one line. There is no runtime lookup table: the game references
each label directly at a screen transition. The tile-index space has no named symbols, so every cell
is a plain `std::uint8_t`.

## The six consumption classes

Twenty-two labels group into six classes by the loader that consumes them and the shape that loader
expects.

### C1 — full-screen maps (20 × 18)

Loaded by `LoadTilemap` (`tetris.asm:6410`): `ld b, SCRN_Y_B` (18 rows) / `ld c, SCRN_X_B`
(20 columns), copying row by row to a background map, advancing the destination by `SCRN_VX_B` (32)
per row. Nine maps: `TypeAGameplayTilemap` (:6938), `TypeBGameplayTilemap` (:6958),
`CopyrightScreenTilemap` (:6985), `TitleScreenTilemap` (:7005), `ConfigScreenTilemap` (:7025),
`TypeADifficultyTilemap` (:7045), `TypeBDifficultyTilemap` (:7065),
`MultiplayerDifficultyTilemap` (:7112), `MultiplayerGameplayTilemap` (:7132). Each is 360 bytes.

### C2 — banner strips (20 wide, few rows tall)

Loaded through `LoadTilemap.columnLoop` (`tetris.asm:6415`) with the caller setting the row count in
`b` and the destination in `hl`. Three maps:

- `MultiplayerVictoryTopTilemap` (:7152) — 4 rows, drawn from `$9800` (`tetris.asm:2376`, `ld b, 4`).
- `MultiplayerVictoryBottomTilemap` (:7158) — 6 rows, drawn from `$9980` (`tetris.asm:2380`,
  `ld b, 6`). It has no `ld de` of its own: the two victory labels are contiguous in the source, so
  when the top banner's copy finishes the source pointer already addresses the bottom banner.
- `BuranBackdropTilemap` (:7106) — 4 rows, drawn from `$9DC0` (`tetris.asm:2738`, `ld b, 4`).

### C3 — playing-field overlays (10 × 18, `$FF`-terminated)

Loaded by `LoadPlayingFieldTilemap` (`tetris.asm:6434`): `ld b, 10` per row, copying into the WRAM
shadow field until a `$FF` sentinel is read (`cp a, $FF`, `tetris.asm:6440`), then it seeds the wipe
(`hWipeCounter = 2`) that copies the shadow field to the screen one row per frame (see
`playing-field.md`). Two maps, each 18 rows of 10 tiles followed by a lone `$FF`:

- `ScoreboardTilemap` (:6489) — charmap text; the baked-in `0` score tiles and `× N` multiplier rows
  are part of the data.
- `DancersTilemap` (:7085) — raw tiles.

The `$FF` is serialization: it terminates the copy, it is not a field cell. The composed grid is the
10 × 18 body; the byte fixture keeps the sentinel (181 bytes).

### C4 — window message blocks (8 wide)

Loaded by `Call_1F7D` (`tetris.asm:4973`), 8 tiles per row (`ld b, $08`, `tetris.asm:4975`), the
caller setting the destination in `hl` and the row count in `c`, advancing 32 per row. Three maps:

- `PauseMessageTilemap` (:6477) — 10 rows, overlaid at `$9C63`.
- `Data_293E` (:6510), ported as `kGameOverTilemap` — 7 rows; the framed "game over" box, mixed
  frame bytes and charmap text.
- `Data_2976` (:6519), ported as `kTryAgainTilemap` — 6 rows; "please try again♥".

### C5 — tower columns (1 wide × 7 tall)

Loaded by `LoadTilemap9C00Row` (`tetris.asm:3101`), which writes **down a column** — one tile,
then advance the destination by 32 (one screen row) and repeat, `b` = 7 at every call site
(`tetris.asm:2698`, :2702, :2742, :2746). Four columns, 7 tiles each:
`LeftTowerLeftSideTilemap` (:3088), `LeftTowerRightSideTilemap` (:3091),
`RightTowerLeftSideTilemap` (:3094), `RightTowerRightSideTilemap` (:3097). Stored top to bottom.

### C6 — congratulations strip (16 tiles)

`.data_12F5`, a local label inside `GameState_2C` (`tetris.asm:2874`), ported as
`kCongratulationsTilemap`. The state prints one tile per six-frame tick, walking a cursor from `$82`
to `$92` (`tetris.asm:2874`–:2913), and generates a `$B6` underline tile in the row beneath each
printed character (`ld b, $B6`, `tetris.asm:2897`). The underline is produced by code — it is not in
the data. Sixteen tiles.

## Character-map resolution

Text rows are `db "string"` operands the assembler encodes through the character map (`charmap.asm`;
see `charmap.md`). The port decodes them by the same **greedy longest-match** the assembler uses, so
a multi-character sequence is consumed whole before its prefix. The one multi-character sequence is
the `.”` ligature (a period followed by a right double quote), which encodes to a single tile `$9D`.
Because of it, the credits line `"by alexey pazhitnov.”"` (`tetris.asm:7002`) is exactly 20 tiles:
the trailing `.”` is one cell, not two. A per-character decode would produce 21 and be wrong.

## Per-label dimensions

| Port constant | Class | Shape | Bytes |
|---|---|---|---|
| `kTypeAGameplayTilemap` | C1 | 20 × 18 | 360 |
| `kTypeBGameplayTilemap` | C1 | 20 × 18 | 360 |
| `kCopyrightScreenTilemap` | C1 | 20 × 18 | 360 |
| `kTitleScreenTilemap` | C1 | 20 × 18 | 360 |
| `kConfigScreenTilemap` | C1 | 20 × 18 | 360 |
| `kTypeADifficultyTilemap` | C1 | 20 × 18 | 360 |
| `kTypeBDifficultyTilemap` | C1 | 20 × 18 | 360 |
| `kMultiplayerDifficultyTilemap` | C1 | 20 × 18 | 360 |
| `kMultiplayerGameplayTilemap` | C1 | 20 × 18 | 360 |
| `kMultiplayerVictoryTopTilemap` | C2 | 20 × 4 | 80 |
| `kMultiplayerVictoryBottomTilemap` | C2 | 20 × 6 | 120 |
| `kBuranBackdropTilemap` | C2 | 20 × 4 | 80 |
| `kScoreboardTilemap` | C3 | 10 × 18 + `$FF` | 181 |
| `kDancersTilemap` | C3 | 10 × 18 + `$FF` | 181 |
| `kPauseMessageTilemap` | C4 | 8 × 10 | 80 |
| `kGameOverTilemap` | C4 | 8 × 7 | 56 |
| `kTryAgainTilemap` | C4 | 8 × 6 | 48 |
| `kLeftTowerLeftSideTilemap` | C5 | 1 × 7 | 7 |
| `kLeftTowerRightSideTilemap` | C5 | 1 × 7 | 7 |
| `kRightTowerLeftSideTilemap` | C5 | 1 × 7 | 7 |
| `kRightTowerRightSideTilemap` | C5 | 1 × 7 | 7 |
| `kCongratulationsTilemap` | C6 | 1 × 16 | 16 |

Corpus total: **4110 bytes**.

## Consumer context (not ported by this unit)

Screen behavior ports at the presentation layer; it is recorded here so the data is understood.

- **Pause dual-map trick.** The gameplay maps are drawn to both background maps (`$9800` and
  `$9C00`); the pause message is overlaid at `$9C63`, and pausing flips the active map with the LCDC
  bit-3 toggle (`tetris.asm:4461`–:4487) so the message appears and disappears without redrawing.
- **Level digit.** Poked at `$98F1` / `$9850` via `hLevelTilemapPointerLo` (`tetris.asm:4144`–:4168).
- **Victory names.** Overwritten at the `$9841` / `$99C1` area after the banner loads
  (`tetris.asm:2383` ff.).
- **Live scores.** Printed over the scoreboard (`tetris.asm:4624` ff.).
- **Tower pokes.** The launch umbilical and crew tunnel tiles are poked at `$9D08` / `$9D28`
  (`tetris.asm:2704`–:2711) after the tower columns load.
- **Congratulations cadence.** One tile per six-frame tick with the generated `$B6` underline, as
  above.

## Surface: parser-emitted vs hand-written

**Generated** from the source by `tools/asm_parser/parse_tilemaps.py`:

- `src/data/generated/tilemaps_data.inc` — the four dimension constants and all 22 composed grids.
- `tests/fixtures/tilemaps_expected.h` — the 22 flat byte arrays in serialization order and form
  (`$FF` sentinels kept), independent of the composed surface.

**Hand-written** port design: `src/data/tilemaps.h` (the header-only surface), the parser and its
tests, this contract, and `tests/test_tilemaps.cpp`.

## Structural guarantees the parser asserts

The generator reads every value from the source and fails hard (with a `file:line` citation) on any
deviation, so a mis-transcription cannot reach the tables:

- The character map parses to its full table first; every `db "…"` run resolves by greedy
  longest-match, and any unmapped character is an error.
- Every label appears exactly once with its class shape: C1 exactly 18 rows of 20; C2 the row count
  its call site declares; C3 exactly 18 rows of 10 then a lone `$FF`; C4 rows of 8; C5 exactly seven
  bytes; C6 exactly sixteen. Mixed string/byte rows resolve per segment and must still land on the
  class width.
- The loader anchors hold: `LoadTilemap`'s `ld b, SCRN_Y_B` / `ld c, SCRN_X_B`; `Call_1F7D`'s
  `ld b, $08`; each tower call site's `ld b, 7` + `call LoadTilemap9C00Row`; each banner call site's
  row count equal to its parsed banner; `LoadPlayingFieldTilemap`'s `ld b, 10` and `cp a, $FF`.
- The per-label byte counts sum to 4110.
