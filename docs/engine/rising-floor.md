# The rising floor

What makes a Type C round different from a Type A one. A count of drops sits on the panel under the
label `RISE`: every drop takes one off it, every row cleared puts one back, and when it reaches zero
the whole stack shifts up a row and a fresh garbage row arrives at the bottom of the field.

## Where it lives

| File | Holds |
|---|---|
| `src/systems/rising_floor.h` | The interval, the seam type, and the four functions below |
| `src/systems/rising_floor.cpp` | The counter law, the shift, and the arriving row |
| `src/data/type_c_tilemap.h` | The Type C gameplay backdrop, including the `RISE` label |
| `src/systems/readouts.cpp` | `printRise` and Type C's panel cells |

## The surface

```cpp
inline constexpr std::uint8_t kTypeCRiseInterval = 10;

using RiseFloorHook = std::function<void(GameContext&)>;

void armRiseCounter(GameContext& game);
void recordLock(GameContext& game, std::uint8_t clearedRows);
void riseFloor(GameContext& game, const std::function<std::uint8_t()>& fold);

[[nodiscard]] RiseFloorHook makeRiseFloorHook(std::function<std::uint8_t()> fold);
```

`armRiseCounter` loads the counter for a fresh round — to a full interval in a Type C round, to zero
in every other mode. The round init calls it *after* the three draws that fill the piece pipeline, so
filling the pipeline does not spend part of the player's first interval.

`recordLock` settles the counter for a drop that has just locked. Every drop costs one; every row it
cleared is credited straight back, and the credit stops at a full interval. So a single line breaks
even, a double gains one, and a tetris gains three — clearing is not a reprieve from the floor, it is
the only thing holding the floor off, and one line a drop is merely staying level. The completed-row
scan calls it on every lock, passing the count it found. It stops at zero rather than wrapping, so the
counter sits at zero from the drop that empties it until the spawn point that acts on it. Outside a
Type C round it does nothing.

`riseFloor` does the move. Every field row takes the contents of the row below it; row 0's own
contents are discarded, which is how a stack that reaches the ceiling loses its top row. The bottom
row is then filled a cell at a time from `fold`. Both the board and the displayed map are written —
the rise appears at once rather than being carried in by a wipe. The walls, the floor, the rows below
the field and the multiplayer attack row are left alone. It also cues the garbage-arriving sound.

`makeRiseFloorHook` packages all of that as the seam the line-clear pipeline takes. The returned hook
fires only when three things are true — the round is Type C, the counter has reached zero, and the
game is in normal gameplay — and then reloads the counter and redraws the panel's countdown.

## Wiring it

The arriving row's cells come from the same source a Type B round's starting garbage uses. Register
it on the same virtual machine as the piece randomizer: there is one divider, and sharing it is what
makes a round's rises part of the same history as its piece draws.

```cpp
retropp::Vm vm{retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy};
const auto drawPiece   = kirpich::vm::registerPieceRandom(vm);
const auto garbageFold = kirpich::vm::registerGarbageFold(vm);
const auto riseFloor   = kirpich::systems::makeRiseFloorHook(garbageFold);
```

Then hand the hook to the two points in the frame's vertical-blank beat where a lock can spawn the
next piece:

```cpp
kirpich::systems::animateLineClear(game, drawPiece, riseFloor);
kirpich::systems::playingFieldWipeTick(game, drawPiece, riseFloor);
```

Both parameters default to an empty hook, and an empty hook raises no floor. That is what every other
mode gets, and what a build that does not wire the seam gets.

## Why it fires at a spawn point

A spawn point is the one moment in a round when no piece is in flight, so the rise can never move the
field under a falling piece or invalidate a collision test halfway through a drop.

It is also downstream of the entire clear pipeline, so a floor that comes up always does so after the
flash, the compaction and the wipe have finished with the field. The credit law makes that ordering
hard to observe — a lock that clears rows credits at least one drop back, so it cannot be the lock
that empties the count — but the seam's position is what guarantees it rather than the arithmetic, and
changing what a cleared row is worth cannot disturb it.

## What to edit

| To change | Edit |
|---|---|
| The count a round starts on, and the ceiling a clear can credit up to | `kTypeCRiseInterval` in `rising_floor.h` |
| What a cleared row is worth | the credit in `recordLock` |
| What the arriving row looks like | the cell source passed to `makeRiseFloorHook` — see `src/vm/garbage_fill.h` |
| The one-gap-per-row guarantee | the forcing branch in `riseFloor` |
| Which sound a rise makes | the cue at the end of `riseFloor` |
| Where the countdown is drawn | `kTypeCRiseRow` / `kTypeCRiseCol` in `readouts.cpp`, and the `RISE` label in `type_c_tilemap.h` |
| When the counter is armed | the Type C branch at the end of `initGame` in `gameplay.cpp` |
