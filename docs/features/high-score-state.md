# High-score state

**Date:** 2026-08-14
**Status:** Complete (state + persistence; game-loop wiring is later work)

## Concept

The top-score surface: the two work-RAM tables the game keeps its high scores in — `wTypeBTopScores`
(`$D000`, 1620 bytes) and `wTypeATopScores` (`$D654`, 270 bytes) — plus the four high-RAM bytes the
score-entry flow reads and writes across frames, expressed as one `HighScoreState` struct with a
`TopScoreEntry` cell type. It is the latest state block and the **first port-side durable state**: the
port persists the tables across launches through the engine's save store, an enhancement over the
original, which keeps them in RAM only.

Behavioral specification (per-byte map, slice layout, insert/name-entry/display mechanisms, quirks,
boot/persistence rules) is in [`../contracts/high-score-state.md`](../contracts/high-score-state.md).
The modification guide is in [`../engine/high-score-state.md`](../engine/high-score-state.md).

## Design decisions

- **One struct header + one persistence pair.** `high_score_state.h` is header-only, dependency-light
  (`<array>`, `<cstdint>`, `char_tile.h`). The persistence surface lives in its own pair
  (`high_score_persistence.{h,cpp}`) so the state header does not pull in the engine's `SaveStore`;
  the pair is the first `src/state/*.cpp` (the header-only streak ends here by design). A sibling of
  the other state structs, not a member of any — aggregation is later wiring.
- **`TopScoreEntry` — decimal score + charmap name.** `uint32_t score` (decimal in memory, BCD only on
  the wire — the same adjudication as `EngineState::score`; the codec owns the conversion) and
  `std::array<CharTile, 6> name` (raw charmap glyphs; the name-entry wheel's vocabulary is fully
  covered by the `CharTile` enum). Nested plain arrays index the tables `[level][height][rank]`
  (Type B) and `[level][rank]` (Type A). Defaulted `operator==` on both types.
- **Six members, four owned HRAM bytes.** `newTopScore` (`$FFC7`) and `topScoresRedrawRequested`
  (`$FFE8`) are `bool` (domain {0,1}); `newScoreRank` (`$FFC8`) is a `uint8_t` counter kept at the
  ROM's inverted wire value (3 = 1st place); `nameEntryColumn` (`$FFC6`) is the name-entry cursor
  column, an overlay field on a byte the game-flow surface owns as `coarseCountdown` (disjoint in time
  — the same split pattern as `topOutLockCount`/`tempPreviewPiece`).
- **No field for the name-cursor pointer.** `$FFC9`/`$FFCA` (and the call-transient `$FFFB`/`$FFFC`
  pointer) are mechanism, not persistent state — the pointer is fully derivable from the table indices
  and recomputed. Contract-recorded, no struct field. *Rejected:* carrying the pointer as a field —
  it would store derivable state and duplicate a value the game-flow surface already owns at those
  addresses.
- **Persistence — ROM-image payload through `SaveStore`.** The payload is the exact 1890-byte ROM wire
  image (Type B block then Type A block, address order), schema version 1, document `"topscores"`.
  *Rejected:* a bespoke serialized-struct format — it would mint a second layout for data whose layout
  is already contract-pinned, and the ROM image is the natural migration baseline. The codec
  (`encodeTopScores`/`decodeTopScores`) is real port code exercising the BCD and name wire rules both
  directions; `decode` refuses any payload not exactly 1890 bytes.
- **Save identity `Kirpich` / `Kirpich` — locked permanently** (user-ruled 2026-08-14, one-way door).
  The per-user save directory is `<platform data dir>/Kirpich/Kirpich/`; changing either string after
  players have saves strands their documents. Named constants in `high_score_persistence.h`;
  `EngineConfig` wiring is later startup work.
- **Load policy — corrupt is never silent.** Absent → boot zeros (first run); present → decode;
  corrupt (`SaveStoreError`) or wrong length → `spdlog` error, run with boot zeros, **leave the
  damaged file in place** (never treated as absent, never proactively overwritten — the file survives
  until the player actually earns a new top score).
- **Persistence is always on, no toggle** (user order). The original's soft-reset-only survival is
  preserved in-sim and contract-recorded; the port strictly extends *when* scores survive, never *how*
  the tables behave. Recorded as a `DESIGN.md` enhancement amendment.

## Implementation details

- **Files:** `src/state/high_score_state.h` (`HighScoreState` + `TopScoreEntry`),
  `src/state/high_score_persistence.{h,cpp}` (codec + `SaveStore` load/save),
  `tests/test_high_score_state.cpp` (7 cases). `src/CMakeLists.txt` gains one line
  (`state/high_score_persistence.cpp`).
- **Wire codec:** score → 3 BCD bytes low-pair-first (ceiling 999999); name → 6 glyph bytes verbatim.
  A slice emits 9 score bytes then 18 name bytes; the image is the Type B table (60 slices) then the
  Type A table (10 slices).
- **Tests (device-free except the store round-trip, which uses `SaveStore::atPath` under a hermetic
  temp dir):** HRAM window pins, WRAM table pins, struct shape + per-byte field resolution, reset,
  wire-value pins, codec round-trip (BCD edges, wrong-length refusal, Type-B-first image), store
  round-trip (save/load, absent→boot, corrupt→`SaveStoreError`→boot with the file left in place).
- **Test baseline:** 128 → **135** (C++). Parser suite unchanged at `Ran 607`. Both red→green flips
  (wire-value pin, codec assertion) demonstrated.
- **No parser delta:** the unit consumes existing `wram_expected.h` / `hram_expected.h` rows only.

## Open questions / future work

- **Game-loop wiring (later).** Load at startup, save on name-entry submit, and the
  `EngineConfig::setActive` identity wiring are game-flow work — nothing can earn a score before the
  loop exists, so no user-visible function is deferred, only its trigger.
- **Insert / name-entry / display mechanisms (later).** The compare/shift/insert, the name-entry
  state machine (letter wheel, heart-mode swap, key repeat, blink), the print staging, and the VBlank
  flush are re-implemented against this struct when the menu / game-over systems are built.
