# High-score state

The top-score surface: the two tables the game keeps its high scores in — one for Type B (indexed by
level, starting height, and rank) and one for Type A (level and rank) — plus the four high-RAM bytes
the score-entry flow reads and writes between frames, held in one `HighScoreState` struct with a
`TopScoreEntry` cell type. A single instance carries every top-score byte; the menu, game-over, and
name-entry systems take a reference to it. Alongside it is a small persistence surface that saves the
tables to disk and loads them back, so top scores survive across launches.

It is an idiomatic C++ surface, not a byte image of the original's RAM. Each score is a decimal
integer (the original stores it as three packed-decimal bytes); each name is six character-map glyphs;
the tables are nested arrays. The exact mapping back to the `$D000` / `$D654` tables and the
`$FFC6`–`$FFE8` bytes, the slice layout, the insert / name-entry / display mechanisms, the quirks the
surface preserves, and the persistence format are the behavioral spec in
[`../contracts/high-score-state.md`](../contracts/high-score-state.md). The design rationale is in
[`../features/high-score-state.md`](../features/high-score-state.md).

The state type is header-only, `namespace kirpich`, included as `"state/high_score_state.h"`. The
persistence surface is a header/source pair, `"state/high_score_persistence.h"`, so the state type
stays free of the engine's save-store dependency. It is a sibling of `GameFlowState`
([`game-state-machine-state.md`](game-state-machine-state.md)) and the other state blocks, not a member
of any.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/state/high_score_state.h` | `TopScoreEntry`, `HighScoreState`, and `reset()` | Hand-written. Edit here to add or reshape a field. |
| `src/state/high_score_persistence.h` / `.cpp` | The wire codec and the save/load calls | Hand-written. Edit here to change the persistence format or policy. |
| `tests/fixtures/wram_expected.h` | The original's work-RAM layout map and access census | **Generated — do not hand-edit.** Shared with the other work-RAM state units. |
| `tests/fixtures/hram_expected.h` | The original's high-RAM layout map and access census | **Generated — do not hand-edit.** Shared with the other high-RAM state units. |

## The types

`TopScoreEntry` is one ranked score-and-name:

```cpp
struct TopScoreEntry {
    uint32_t score = 0;               // decimal; ceiling 999999
    std::array<CharTile, 6> name{};   // character-map glyphs, first glyph first
    friend bool operator==(const TopScoreEntry&, const TopScoreEntry&) = default;
};
```

`HighScoreState` is the top-score block — the two tables and four session bytes:

```cpp
struct HighScoreState {
    std::array<std::array<std::array<TopScoreEntry, 3>, 6>, 10> typeB{};  // [level][height][rank]
    std::array<std::array<TopScoreEntry, 3>, 10>                typeA{};  // [level][rank]

    bool    newTopScore = false;              // a game just earned a top score (routes into name entry)
    bool    topScoresRedrawRequested = false; // the staged rows need flushing to VRAM next frame
    uint8_t newScoreRank = 0;                 // rank of the new score — 3 = 1st, 2 = 2nd, 1 = 3rd
    uint8_t nameEntryColumn = 0;              // name-entry cursor column, 0..5

    void reset();
    friend bool operator==(const HighScoreState&, const HighScoreState&) = default;
};
```

Every member is zero-initialised, so a default-constructed instance is the boot state, and the struct
has a defaulted `==`. Rank 0 in each table is the best score for that (level[, height]); rank 2 is the
worst. The three entries display in rank order.

## Using it

```cpp
#include "state/high_score_state.h"

kirpich::HighScoreState scores;   // all-zero: the boot state, no scores set

// The best Type A score for level 5:
const kirpich::TopScoreEntry& best = scores.typeA[5][0];

// A Type B slot is chosen by level and starting height:
kirpich::TopScoreEntry& slot = scores.typeB[level][height][rank];
slot.score = 99999;
slot.name  = { kirpich::CharTile::LETTER_A, kirpich::CharTile::LETTER_B, /* ... */ };

// After a game earns a top score, newTopScore routes the menu into name entry; clear it on submit.
if (scores.newTopScore) { /* enter name at newScoreRank, walk nameEntryColumn 0..5 */ }

scores.reset();   // return everything to the boot state
```

## Persistence

The two tables can be saved to and loaded from the engine's durable store. The save is one named,
versioned document; the payload is the exact 1890-byte table image.

```cpp
#include "state/high_score_persistence.h"
#include <retropp/save_store.h>

retropp::SaveStore store;   // resolves the per-user save directory from the app identity

kirpich::saveTopScores(scores, store);   // write the two tables to the "topscores" document

kirpich::HighScoreState loaded;
kirpich::loadTopScores(store, loaded);   // fill the tables from disk, or leave them at boot
```

- **`saveTopScores`** returns true when the document is durably on disk (the store's write is atomic).
- **`loadTopScores`** returns true when it filled the tables from a valid document. On the ordinary
  first run (no document yet) it returns false and leaves the tables at boot. If the document is
  present but corrupt or the wrong length, it logs an error, leaves the tables at boot, and leaves the
  file in place — it never treats a damaged save as absent and never overwrites it.
- Only the two tables persist. The four session bytes (`newTopScore`, `topScoresRedrawRequested`,
  `newScoreRank`, `nameEntryColumn`) are never written or read.
- The codec is also usable directly: `encodeTopScores(state)` returns the 1890-byte image,
  `decodeTopScores(image, state)` fills the tables from one (returning false, and leaving the state
  untouched, if the image is not exactly 1890 bytes).

The save directory is `<platform data dir>/Kirpich/Kirpich/`. The organization and application names
are `kSaveOrganization` / `kSaveApplication` in `high_score_persistence.h`; **do not change them** — a
different identity is a different directory, and existing players' saves stay under the old one.

## Gotchas

- **`newScoreRank` is inverted.** 3 is first place, 2 is second, 1 is third — the original's counter
  runs downward. Read it as a rank, not a 0-based index.
- **`nameEntryColumn` shares its byte with the game-flow state.** The original overlays the name-entry
  column and a gameplay countdown on the same high-RAM byte; the two never run at once. `GameFlowState`
  carries the countdown as `coarseCountdown`; this surface carries the column. They are independent
  fields — do not assume one from the other.
- **The name-cursor pointer has no field.** The original stores a pointer to the name cell being
  entered; it is fully derivable from the table indices and is recomputed, not stored. There is no
  member for it.
- **Scores are decimal; the wire is packed-decimal.** `score` is a plain `uint32_t` with a 999999
  ceiling. The three-byte packed-decimal form only exists inside the codec; you never handle it.
- **Names hold raw glyphs.** `name` is six `CharTile` values; a short name is padded with the
  character map's `$00` glyph. The name-entry wheel's letters, `"×"`, and the heart glyph are all in
  the `CharTile` enum ([`charmap.md`](charmap.md)).
- **This is the state block, not the score logic.** Nothing here compares a score, shifts the table,
  runs the name-entry wheel, or draws anything — those are the menu and game-over systems' job, and
  they take a reference to a `HighScoreState`.

## The layout + census fixtures

`HighScoreState` shares the work-RAM and high-RAM layout+census fixtures with the other state units;
this unit adds nothing to them. Regenerate them after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_wram.py --source-root ../tetris --all \
  --fixture-out tests/fixtures/wram_expected.h
python3 tools/asm_parser/parse_hram.py --source-root ../tetris --all \
  --fixture-out tests/fixtures/hram_expected.h
```

Each parser walks its RAM map deriving every label's address and size, then scans the game code for
every raw-operand access. Python 3 (standard library only); they are development tools and are never
needed to build or test Kirpich.

## Changing it

To add or reshape a field, edit `src/state/high_score_state.h` and give the new member a zero default
so `reset()` and the default constructor stay correct. The two tables and the labelled bytes have
widths pinned against the generated fixtures; a field for a byte the original reaches by a raw numeric
address requires the contract to name its owner — add the mapping to
[`../contracts/high-score-state.md`](../contracts/high-score-state.md) and the owned-byte table in the
test in the same change. To change the persistence format, edit the codec in
`high_score_persistence.cpp`; a format change that must remain loadable from older saves is a schema
version bump plus a migration step registered on the store.

## Testing

`tests/test_high_score_state.cpp` pins the two tables and the four owned bytes against the layout
fixtures, resolves every owned byte to exactly one field (with a negative guard on the derivable
name-cursor pointer bytes), checks `reset()` returns a fully-mutated instance to boot, pins the wire
values (the name-entry glyphs, the `$00` delimiter, the rank domain), and round-trips the persistence
codec and the save store — including the wrong-length refusal, an absent document, and a corrupt
document surfacing as an error while the file is left in place. The store round-trip uses a temporary
directory and removes it afterward. The fixture parsers have their own tests
(`tools/asm_parser/test_parse_wram.py`, `test_parse_hram.py`).
