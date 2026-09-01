# Statistics

What has been played, kept per difficulty combination and across launches. This page covers the
table, the calls that fill it, the folds that read it back, and where each call is wired.

## The shape of the record

One `StatSlice` per difficulty combination (`src/state/stats_state.h`):

```cpp
struct StatSlice {
    std::uint32_t rounds, seconds, longestRoundSeconds, drops,
                  score, lines, singles, doubles, triples, tetrises;
};
```

`seconds` is time actually played there, pauses excluded. `longestRoundSeconds` is the largest single
round played there. Every count saturates at its ceiling rather than wrapping.

The slices sit in three tables, indexed the way the top-score tables are:

```cpp
std::array<StatSlice, kStatLevels>                           typeA;  // [level]
std::array<std::array<StatSlice, kStatVariants>, kStatLevels> typeB;  // [level][start height]
std::array<std::array<StatSlice, kStatVariants>, kStatLevels> typeC;  // [level][rise index]
```

`kStatLevels` is 10 and `kStatVariants` is 6, so there are 130 slices. Type A is picked by level
alone and has no second axis.

`StatsState` holds those three, plus `applicationSeconds` — how long the program itself has run,
which belongs to no round and is therefore the one figure not derived from the tables — plus the
round in progress and the session's own timing. Those last are working values and are not saved.

## Recording

Every call is in `kirpich::systems` (`src/systems/stats.h`).

```cpp
void beginRound(GameContext&, std::uint64_t nowNanos);
void endRound(GameContext&, std::uint64_t nowNanos);
void pauseRound(GameContext&, std::uint64_t nowNanos);
void resumeRound(GameContext&, std::uint64_t nowNanos);
void recordDrop(GameContext&);
void recordLineClear(GameContext&, std::uint8_t rows);
```

`beginRound` latches which combination the round is at and stamps the clock. `endRound` adds the
round, its played time, and its score to the latched slice, and lets its length compete for that
slice's longest. `recordDrop` and `recordLineClear` add to the latched slice as play happens.

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

A rollup is a fold over slices: the nine running counts add and `longestRoundSeconds` takes the
larger of the two. **Summing that field is the mistake to avoid** — it reports a length no round ever
had.

`longestRound` walks Type A by level, then Type B and Type C by level and then by their second axis,
and returns the largest with the slice it was found in. `any` is false when nothing has been played.
Ties keep the first slice in that walk, so the answer does not move between calls.

`RoundCombination` (`src/state/game_flow_state.h`) carries `type`, `level`, `variant` and
`hasVariant`; Type A leaves `hasVariant` false, which is what makes its label read `A / 1` where the
other two read `B / 1 / 3`.

### Showing a duration

```cpp
struct DurationText { std::array<char, 16> chars; std::uint8_t size;
                      std::string_view view() const; };
DurationText formatDuration(std::uint32_t seconds);
```

Carried by value, so it can go straight to a text write without outliving the call. The font has no
colon: an hour and up reads `2h 05m`, below an hour reads `5m 03s`.

## Which combination a round is at

```cpp
RoundCombination combinationOf(const GameFlowState&);
```

One derivation, in `src/state/game_flow_state.h`, used by both the statistics and the top-score slice
selection. A round's score and a round's counts have to reach the same slice; two readings of the
same flow state could drift apart. A game type this does not recognise resolves to Type A, which is
the fall-through the top-score selection takes.

## Persistence

`src/state/stats_persistence.h` — document `"stats"`, schema version 1, a fixed 5204-byte image: the
Type B block, then Type A, then Type C (the top-score document's own order), then the application
total. A slice is its ten counts as little-endian 32-bit values in declaration order.

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
| `recordDrop` | `lockPieceIntoBackground` (`src/systems/piece.cpp`) |
| `recordLineClear` | the per-kind tally in `src/systems/line_clear.cpp` |
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
2. Raise `kStatSliceBytes` by four. `kStatsImageBytes` follows from it.
3. Raise `kStatsSchemaVersion` and register a migration in `loadStats` that grows an older image.
4. Add the field to `fold` in `stats.cpp` — `addSaturating` for a running count, `std::max` for
   anything that is a largest-so-far.
5. Record it wherever it happens, through a call that returns early while no round is open.
