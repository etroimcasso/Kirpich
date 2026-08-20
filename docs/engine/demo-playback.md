# Demo playback

The attract-mode demos — the two recorded rounds the game plays to itself at the title screen.

## Where it lives

| Path | What |
|---|---|
| `src/systems/demo.h` | The six routines and the hook set |
| `src/systems/demo.cpp` | Implementations |
| `src/data/demo.h` | The two recordings and the shared piece list (generated) |
| `src/state/demo_state.h` | Everything a demo persists between frames |
| `tests/test_demo_playback.cpp` | Behavioral tests |
| `docs/contracts/demo-playback.md` | What the behavior is, with source line anchors |

## The flow

```
title screen idles
  └─ attract countdown hits zero
       └─ startDemo          configure, load the screen, enter the round init
            └─ the ordinary round runs, frame by frame:
                 checkForEndOfDemo      Start pressed, or the last piece played?
                 demoSimulateJoypad     advance the recording, substitute its input
                 recordDemo             (dead — the flag is never set)
                 ...the piece, line-clear and scoring steps...
                 restoreDemoSavedJoypad give the player their buttons back
       └─ either terminal → the title screen init, and the next demo is the other one
```

## The surface

```cpp
void startDemo(GameContext&);               // configure and launch the next demo
void checkForEndOfDemo(GameContext&);       // Start, or the recording's last piece
void demoSimulateJoypad(GameContext&);      // advance the recording, substitute its input
void recordDemo(GameContext&);              // the dead recording path
void restoreDemoSavedJoypad(GameContext&);  // undo the substitution
void startRecordingDemo(GameContext&);      // arms recording; nothing calls it

std::span<const DemoInputRecord> demoTimeline(ActiveDemo);  // the live recording
GameplayDemoHooks demoHooks();                              // the four seams, bound
```

Every routine returns immediately when no demo is running, so they are safe to call every frame — which
is what the gameplay frame does.

## Installing

Two bindings, both in `main()`:

```cpp
installTitleScreenHandlers(dispatcher, startDemo);

installGameplayHandlers(dispatcher, GameplayWiring{
    .draw = ...,
    .demo = demoHooks(),
    .initGarbage = ...,
});
```

Both parameters default to empty. A build that omits them still runs — the title screen simply idles
instead of playing a demo.

## The recordings

Two timelines, in `src/data/demo.h`, generated from the ROM. Each step is a set of actions and a frame
count:

```cpp
{ .held = heldActions({Action::MoveRight, Action::RotateClockwise}), .frames = 0x04 }
```

Playback holds a step's input for its frame count, then loads the next. The cursor on `DemoState` is
an index into whichever timeline is live; `demoTimeline` returns it.

The recordings do not end themselves — a demo ends on its piece count, so the trailing steps of each
timeline are never reached.

## Two things that are easy to get wrong

**The presses come from the recording's own history.** `demoSimulateJoypad` derives newly-pressed as
*held now and not held in the recording's previous step*. It deliberately does not go through
`InputSystem::sample`, whose history holds the player's real buttons — routing it there reports presses
the recording never made and corrupts the live input path for the next frame.

**The order of the four routines is behavior.** The end check runs before the substitution so it reads
the player's Start; the restore runs after everything that consumes input. The gameplay frame already
calls them in that order — if you add a step, put it where the contract says.

## The sound during a demo

The sound driver reads the running-demo byte directly and blanks every cue mailbox while it is set, so
a demo triggers no effects and no music changes of its own. The song already playing continues — the
title music carries into the demo.

One consequence looks like a bug and is not: the title screen re-cues its music every time it is
entered, but a demo ends by entering it while the running-demo byte is *still set*, so that cue is
blanked. The title song plays on a cold start and after a real round, never on the way back from a
demo. Clearing the byte earlier would "fix" it and would be wrong — see the contract.

## Changing things

| To change | Edit |
|---|---|
| Which demo plays first, or the alternation | `startDemo` — the branch reads the demo that ran *last* |
| The level or starting height a demo runs at | The constants in `src/systems/demo.h` |
| When a demo ends | `kTypeADemoEndPieceCount` / `kTypeBDemoEndPieceCount`; the test is equality, not a threshold |
| How long the title screen waits | The attract counter in `src/systems/title_screens.cpp` |
| The recorded input itself | Not by hand — the timelines are generated from the ROM |

Behavior here is pinned to the original. Before changing any of it, read
`docs/contracts/demo-playback.md` — most of what looks like an oddity is load-bearing, and the tests
assert it.
