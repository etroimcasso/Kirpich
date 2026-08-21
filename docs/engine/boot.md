# The boot path

How the machine starts, and what the four-button reset chord runs. The behavioral specification —
what the original game does, line by line — is in [`../contracts/boot.md`](../contracts/boot.md);
the design rationale is in [`../features/boot.md`](../features/boot.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/boot.h` / `.cpp` | `coldBoot`, `softReset`, `bootGame`, and the file-local background-map fill. |
| `src/main.cpp` | Calls `bootGame` once at startup and installs the reset into both of its seams. |
| `tests/test_boot.cpp` | The behavioral tests. |

## The surface

```cpp
namespace kirpich::systems {

void coldBoot(GameContext& game);
void softReset(GameContext& game);
void bootGame(GameContext& game, retropp::SaveStore& saves);

}
```

`coldBoot` returns the whole state aggregate to its power-on values, fills the first background map
with the character map's space glyph, leaves the second map zeroed, requests the sound driver's
startup, and writes the three values the following screens read — Type A, music A, and the copyright
screen as the state to dispatch next.

`softReset` is `coldBoot` with the two top-score tables carried across it. Everything else, including
the four score-entry bytes that live alongside those tables in `HighScoreState`, returns to boot.

`bootGame` is `coldBoot` followed by a read of the player's saved top scores. It exists so that
ordering lives in one place: the boot clears the tables, so the load has to follow it.

## Wiring

```cpp
kirpich::systems::GameContext        game;
retropp::SaveStore                   saves;
kirpich::systems::GameStateDispatcher dispatcher;

// One closure, both seams — the original reaches one reset routine from both of the places that
// detect the chord. The dispatcher's own reset goes with it, so the frame after a reset reads every
// held button as freshly pressed.
const auto reset = [&game, &dispatcher] {
    kirpich::systems::softReset(game);
    dispatcher.reset();
};
dispatcher.softReset = reset;

kirpich::systems::installGameplayHandlers(
    dispatcher, kirpich::systems::GameplayWiring{ /* … */ .softReset = reset });

kirpich::systems::bootGame(game, saves);
```

Both seams matter. The frame dispatcher tests the chord every tick, and the gameplay frame tests it a
second time, because the original does both. Leaving either unassigned makes the chord do nothing in
that path.

A matched chord also ends the gameplay frame: `handleStartSelect` returns `false` and `normalGameplay`
stops there, so the piece, the line-clear scan, the lock and the score award do not run on a frame the
machine was reset in. That return value is `[[nodiscard]]` — every caller has to decide what to do
with it.

## What the boot does not do

The original's startup routine spends roughly a third of its length writing display, interrupt, stack
and timer registers. This port draws through a display the engine owns and takes its frame from the
engine's run loop, so those writes have nothing to reach and produce no code here.
[`../contracts/boot.md`](../contracts/boot.md) §4 accounts for every line of that routine and what
became of it — check there before concluding something was dropped.

The sound driver's own startup (switching the sound hardware on, clearing the driver's work RAM, and
its initialisation call) is real and does run, but it lives in the driver's startup routine rather than
here; `coldBoot` asks for that whole startup to be run again by setting the driver-restart request the
frame's sound step consumes.

**Not the plain initialisation the game asks for elsewhere.** That entry leaves the driver's pause-tune
timer set, and while it is set the driver plays the pause tune and never reaches its sound routines —
so a reset that only initialised would silence every effect and all music for the rest of the session.
The work-RAM clear is what puts that byte back, and only the whole startup performs one. See
[`sound-driver.md`](sound-driver.md).

## Changing behavior

- **What a boot leaves behind** — the three assignments at the end of `coldBoot`. They carry the
  original's byte values through the `GameType`, `MusicType` and `GameState` enumerators.
- **What a reset preserves** — the two members saved and restored in `softReset`. Save by member, not
  by structure: `HighScoreState` also holds four bytes that a reset is supposed to clear.
- **The background map fill** — `clearFirstBackgroundMap` in `src/systems/boot.cpp`. It fills the
  first map only, and with the space glyph rather than zero.
- **The startup order** — `bootGame`. The clear precedes the load; reversing them would wipe the
  scores the launch just read.
- **Which buttons the chord is** — not here. The chord is tested in
  `src/systems/game_state_dispatcher.cpp` and again in `src/systems/gameplay.cpp`; this unit supplies
  only what happens once one matches. See [`dispatcher.md`](dispatcher.md).

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^Boot\.'
```

Seven of the eight tests are device-free. The eighth needs a save store and builds a hermetic one in a
temporary directory.
