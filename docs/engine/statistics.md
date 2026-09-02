# Statistics

What has been played, kept per difficulty combination and across launches. This page covers the
table, the calls that fill it, the folds that read it back, the screens that show it, and where each
call is wired.

## The shape of the record

One `StatSlice` per difficulty combination (`src/state/stats_state.h`):

```cpp
struct StatSlice {
    std::uint32_t rounds, seconds, longestRoundSeconds, drops,
                  score, lines, singles, doubles, triples, tetrises;
    std::array<std::uint32_t, kPieceKindCount> pieces;  // indexed by PieceKind
};
```

`seconds` is time actually played there, pauses excluded. `longestRoundSeconds` is the largest single
round played there. `pieces` is how many of each shape have come to rest there, counted at the same
moment as `drops`, so the seven sum to it exactly. Every count saturates at its ceiling rather than
wrapping, and every one is 32 bits — a narrower per-shape count would stop climbing while `drops`
kept going, and the two are shown on the same screen.

The slices sit in three tables, indexed the way the top-score tables are:

```cpp
std::array<StatSlice, kStatLevels>                           typeA;  // [level]
std::array<std::array<StatSlice, kStatVariants>, kStatLevels> typeB;  // [level][start height]
std::array<std::array<StatSlice, kStatVariants>, kStatLevels> typeC;  // [level][rise index]
```

`kStatLevels` is 10 and `kStatVariants` is 6, so there are 130 slices. Type A is picked by level
alone and has no second axis.

`StatsState` holds those three, plus the two figures that are not folds over them:
`applicationSeconds`, how long the program itself has run, which belongs to no round; and
`musicRounds`, how many rounds have been played under each music selection, indexed by
`musicTypeIndex` — a song is not part of a combination, so there is nowhere in a slice for it to
live. It also holds the round in progress and the session's own timing, which are working values and
are not saved.

## Recording

Every call is in `kirpich::systems` (`src/systems/stats.h`).

```cpp
void beginRound(GameContext&, std::uint64_t nowNanos);
void endRound(GameContext&, std::uint64_t nowNanos);
void pauseRound(GameContext&, std::uint64_t nowNanos);
void resumeRound(GameContext&, std::uint64_t nowNanos);
void recordDrop(GameContext&);
void recordPiece(GameContext&, PieceKind kind);
void recordLineClear(GameContext&, std::uint8_t rows);
```

`beginRound` latches which combination the round is at, stamps the clock, and counts the round under
the music it is being played to. `endRound` adds the round, its played time, and its score to the
latched slice, and lets its length compete for that slice's longest. `recordDrop`, `recordPiece` and
`recordLineClear` add to the latched slice as play happens; `recordPiece` is called from the same
place and for the same event as `recordDrop`, which is what makes the seven per-shape counts sum to
the drop count, and a kind outside the seven is ignored rather than written anywhere.

Three properties are worth knowing before wiring a new call site:

- **`beginRound` refuses while a demo is running.** It leaves the round closed, and every other call
  above does nothing while no round is open. That single gate is the whole of the attract-demo
  exclusion — a new recording call needs no gate of its own.
- **`endRound` is idempotent.** Calling it when no round is open does nothing, so several states may
  each call it without a round being counted twice. That is what lets a handler that runs every frame
  call it unconditionally on entry.
- **`beginRound` closes a round left open**, into the combination that round was started at. An
  abandonment costs a late close, never a leak into the next round.

The clock reaches the handlers as a seam:

```cpp
using NowNanos = std::function<std::uint64_t()>;
```

Without one, rounds are still counted; they are simply zero seconds long.

### The program's own time

```cpp
void beginSession(GameContext&, std::uint64_t nowNanos);
void bankApplicationTime(GameContext&, std::uint64_t nowNanos);
```

`beginSession` starts the session's clock; the host calls it once, after the saved totals are loaded.
`bankApplicationTime` folds everything since the last call into `applicationSeconds` and keeps the
part of a second that does not yet make a whole one, so a run of short banks adds up to what one long
one would. Call it wherever the game already writes to disk.

## Reading it back

```cpp
StatSlice totalsFor(const StatsState&, GameType);
StatSlice lifetimeTotals(const StatsState&);

struct LongestRound { std::uint32_t seconds; RoundCombination at; bool any; };
LongestRound longestRound(const StatsState&);
```

A rollup is a fold over slices: the running counts add and `longestRoundSeconds` takes the larger of
the two. **Summing that field is the mistake to avoid** — it reports a length no round ever had.

`longestRound` walks Type A by level, then Type B and Type C by level and then by their second axis,
and returns the largest with the slice it was found in. `any` is false when nothing has been played.
Ties keep the first slice in that walk, so the answer does not move between calls.

`RoundCombination` (`src/state/game_flow_state.h`) carries `type`, `level`, `variant` and
`hasVariant`; Type A leaves `hasVariant` false, which is what makes its label read `a-5` where the
other two read `b-1-3`.

The remaining folds answer the all-time page, and each is an argmax over rounds played:

```cpp
std::uint32_t  roundsFor(const StatsState&, GameType);
FavouriteMode  favouriteMode(const StatsState&);
FavouriteMusic favouriteMusic(const StatsState&);
PreferredLevel preferredLevel(const StatsState&);   // across all three game types
```

Each result carries its own `any`, false when nothing has been played, so a caller shows that rather
than a first-slot default reading as a real answer. Ties go to the first in walk order — game types
A, B, C; levels from 0 up; music selections from A up — which is the rule `longestRound` follows.

One more fold reads a picker's two axes:

```cpp
struct StatSelection { GameType type; std::uint8_t level, variant; };  // kStatAxisAll folds an axis
StatSlice totalsForSelection(const StatsState&, const StatSelection&);
```

`kStatAxisAll` (`src/state/screen_ui_state.h`) sits outside the ten levels and six variants rather
than at index 0, because 0 is a level a player can pick. **Both axes folded returns exactly what
`totalsFor` returns for that type**, and a test asserts the two against each other: two folds that
could disagree would be two answers to one question. Type A has no second axis, so its variant is not
consulted whatever it holds, and a value outside an axis folds that axis rather than selecting
nothing.

### Showing a duration

```cpp
struct DurationText { std::array<char, 16> chars; std::uint8_t size;
                      std::string_view view() const; };
DurationText formatDuration(std::uint32_t seconds);
```

Carried by value, so it can go straight to a text write without outliving the call. The font has no
colon: an hour and up reads `2h 05m`, below an hour reads `5m 03s`.

The font is the constraint on every string these screens draw. Inside the range both tile sets share
it has letters, digits, a period and a hyphen — no slash, no colon, no comma, no apostrophe — and
`writeMapText` draws **nothing at all** for text it cannot spell, so an unspellable string is a blank
row rather than a compile error. That is why a combination reads `b-1-3`.

## The screens

Three machines and one content unit:

| Unit | What it is |
|---|---|
| `src/systems/list_screen.h` | A heading, a window of rows, a cursor that walks them, a row to act on |
| `src/systems/page_screen.h` | A heading, a body the caller fills, up and down turning pages |
| `src/systems/stats_pages.h` | Which pages each branch has, what is on them, and the picker |
| `src/render/stats_pages.h` | The seven shapes on a pieces page |

The statistics screen is a list instance with five rows; every one of them opens **one** page-screen
instance, which forks on `ScreenUiState::statsBranch`. The paged screen never holds a line: it clears
the map, writes the heading, and asks the caller to fill the page, so a figure is computed as it is
drawn and nothing has to outlive the call.

```cpp
struct PageWiring {
    std::function<std::size_t(const GameContext&)>                        count;
    std::function<std::string_view(const GameContext&, std::size_t page)> title;
    std::function<void(GameContext&, std::size_t page)>                   paintPage;
    std::function<void(GameContext&)>                                     back;
    std::function<bool(GameContext&, std::size_t page, int delta)>        walk;
    std::function<void(GameContext&, std::size_t page, int delta)>        adjust;
};
```

`paintPage` writes into `game.display.displayedMap()` — the map the screen has just cleared. An unset
`back` pops the navigation stack. `walk` is offered every vertical step **before** the page turns and
returns true when it took it, which is what lets a page that owns rows make its own walk and the page
turn one motion; `adjust` is left and right, which the machine has no meaning of its own for.

Three things a new instance depends on:

- **The machine draws no cursor.** A readout has no selectable row. A page that does carry one draws
  it as part of its own body, where every other mark on the page comes from.
- **It empties the object buffer on the way in and on the way out.** A screen it was opened over is
  returned to at its *loop* slot rather than through its init, so nothing there would clear what this
  page placed — a picker's arrows would still be standing on a screen that has no picker.
- **Up is not a way out.** B leaves a screen, here as everywhere else in the game.

### The picker

A game type's two pages share one selection, which is what makes the display change under it. Its
rows are scrollers in the settings screen's own columns; each axis opens on `kStatAxisAll`, so the
mode's aggregate, one level across its variants, and a single combination are one control scheme and
none of them needs a page of its own. A rise is shown as the interval the player picked — 16, 14, 12,
10, 8, 6 — never as the 0-5 index it is stored at.

### Drawing a line

```cpp
void statLine(BackgroundMap&, std::size_t line, std::string_view label,
              std::uint32_t value, std::uint8_t digitPairs);
void statTextLine(BackgroundMap&, std::size_t line, std::string_view label,
                  std::string_view value);
```

The label starts at `kStatsLabelCol` and the value ends at `kStatsValueEndCol`, so the numbers line up
down the page. **The width is per figure and has to be chosen for what it draws**: `drawNumber` drops
digits above its width rather than widening the field. A lifetime score wants five pairs and leaves
seven cells for its label; a clear count is happy with three and leaves eleven.

## Which combination a round is at

```cpp
RoundCombination combinationOf(const GameFlowState&);
```

One derivation, in `src/state/game_flow_state.h`, used by both the statistics and the top-score slice
selection. A round's score and a round's counts have to reach the same slice; two readings of the
same flow state could drift apart. A game type this does not recognise resolves to Type A, which is
the fall-through the top-score selection takes.

## Persistence

`src/state/stats_persistence.h` — document `"stats"`, schema version 2, a fixed 8860-byte image: the
Type B block, then Type A, then Type C (the top-score document's own order), then the application
total, then the per-music round counts. A slice is its seventeen counts as little-endian 32-bit
values in declaration order. Version 1 held ten counts a slice and no music block; its migration
widens each slice in place and appends the block, so a document written before the per-shape counts
existed loads with those counts at zero.

```cpp
bool saveStats(const StatsState&, retropp::SaveStore&);
bool loadStats(retropp::SaveStore&, StatsState&);
```

An absent document is an ordinary first run and leaves the tables empty. A corrupt or wrong-length
one is logged, leaves the tables empty, and leaves the damaged file where it is.

**`loadStats` declares the schema version on the store before reading, and a later version must
register its own migrations there too.** The version and the migration chain belong to the store, not
to the document, and this store also carries the settings and the top scores at versions of their
own — so whichever loader is about to read has to be the one that last said which version it means.

## Where each call is wired

| Call | Site |
|---|---|
| `beginRound` | `initGame` (`src/systems/gameplay.cpp`), before the score is cleared |
| `endRound` | `initGameOver`; `typeBVictoryJingle` and `initBonusEnding` (`src/systems/type_b_ending.cpp`), the two states a won Type B round reaches; the reset closure and the exit guard in `src/main.cpp` |
| `pauseRound` / `resumeRound` | `handleStartSelect` (`src/systems/gameplay.cpp`), both the solo and the master paths |
| `recordDrop`, `recordPiece` | `lockPieceIntoBackground` (`src/systems/piece.cpp`), the same site |
| `recordLineClear` | the per-kind tally in `src/systems/line_clear.cpp` |
| the screens | `installStatsScreens` (`src/systems/stats_screens.cpp`), from `src/main.cpp` |
| `beginSession`, `bankApplicationTime` | `src/main.cpp` |
| `loadStats` | `bootGame` (`src/systems/boot.cpp`) |
| `saveStats` | `src/main.cpp`, on a submitted top score and in the exit guard |

The exit guard is a `RunLoop::exitAction`. Every exit source routes through it — the settings
screen's exit row, the window's close button, and the platform's quit gesture — so it is the one
place a round in progress is closed before the program tears down.

`softReset` (`src/systems/boot.cpp`) carries `StatsState` across the reset chord, whole. The session's
stamp is part of what survives: zeroing it would leave the next reading measuring from the clock's
origin.

## Adding a count

1. Add the field to `StatSlice` and to `putSlice` / `takeSlice` in `stats_persistence.cpp`, in the
   same order.
2. Raise `kStatSliceCounts`. `kStatSliceBytes` and `kStatsImageBytes` follow from it.
3. Raise `kStatsSchemaVersion` and register a migration in `loadStats` that grows an older image.
4. Add the field to `fold` in `stats.cpp` — `addSaturating` for a running count, `std::max` for
   anything that is a largest-so-far.
5. Record it wherever it happens, through a call that returns early while no round is open.
6. Show it: a `statLine` on the page it belongs to (`stats_pages.cpp`), at a width wide enough for
   what it draws.

## Adding a page

A page is a row in the branch's title table and a case in its paint. Raise nothing else: the count
comes from the table's own size, the screen records it for the arrows, and the picker is unaffected.
A page that carries the picker starts its own lines at `kStatsModeFirstLine`; one that does not
starts at `kStatsFirstLine`.
