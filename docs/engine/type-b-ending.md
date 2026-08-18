# Type B ending

The three states a won Type B round passes through: the scoreboard, the dance layout, and the dance
itself. The behavioral specification — what the original game does, line by line — is in
[`../contracts/type-b-ending.md`](../contracts/type-b-ending.md); the design rationale is in
[`../features/type-b-ending.md`](../features/type-b-ending.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/type_b_ending.h` / `.cpp` | The `kirpich::systems` ending handlers, the music-playing seam, and the file-local screen loader. |
| `tests/test_type_b_ending.cpp` | The behavioral tests. |

The functions take a `GameContext&` (`src/systems/game_context.h`) and read or write through it. They
own no state: the board lives on `PlayingFieldState`, the performers and piece sprites on
`SpriteRendererState`, the score and object buffer on `EngineState`, the timers, level, start height,
line count, and state on `GameFlowState`, and the jingle cue on the `AudioCues` member.

## The flow

```
level 9 ──► $22 initBonusEnding ──► $23 dancers ──┬── height 5 ──► $26 (Buran launch)
                                                  └── otherwise ──► $05 typeBVictoryJingle
level 0-8 ──────────────────────────────────────────────────────► $05 typeBVictoryJingle ──► $0B
```

`$05` hands off to the Type B results count-up (`initTypeBScoreboard`, see
[`gameplay.md`](gameplay.md)), which drives the tally in [`scoring-system.md`](scoring-system.md). The
line-clear terminal picks the entry state — see [`line-clear.md`](line-clear.md).

## The surface

```cpp
namespace kirpich::systems {

// Whether the sound driver reports a song still playing. The dance holds until it stops.
// No query means no song, which ends the dance.
using MusicPlayingQuery = std::function<bool()>;

// The scoreboard: draw the scoreboard screen into the field, print each line-clear kind's value for
// this round's level, zero the score, hide both piece sprites, re-initialise the sound driver, and
// hand off to the results count-up.
void typeBVictoryJingle(GameContext& game);

// Lay out the dance: draw the backdrop, load the ten performers, seed each with its animation period,
// reveal one more than the starting garbage height, and cue that height's jingle.
void initBonusEnding(GameContext& game);

// The dance: step each performer's animation, flipping its sprite when the counter runs out. Runs
// until `musicPlaying` reports the jingle has ended, then leaves for the Buran launch or the
// scoreboard.
void dancers(GameContext& game, const MusicPlayingQuery& musicPlaying = {});

void installTypeBEndingHandlers(GameStateDispatcher& dispatcher, MusicPlayingQuery musicPlaying = {});

}  // namespace kirpich::systems
```

## Installing the handlers

`installTypeBEndingHandlers` puts the three handlers in their dispatch slots. Pass the music query so
the dance ends when its jingle does:

```cpp
kirpich::systems::SoundSystem sound;

kirpich::systems::installTypeBEndingHandlers(
    dispatcher, [&sound] { return sound.currentMusic().has_value(); });
```

`sound` must outlive the dispatcher — the callable holds a reference to it. Installing without the
query is valid and useful in a build with no audio, but the dance then ends on its first animating
frame instead of running to the music.

## The two screens

Both are field-shaped tilemaps copied into the board at the field's top-left cell, and the copy also
sets the wipe step to 2 — the animation the line-clear system's stepper runs over the new screen.

| State | Screen |
|---|---|
| `$05` | `kScoreboardTilemap` |
| `$22` | `kDancersTilemap` |

Both come from [`tilemaps.md`](tilemaps.md). The scoreboard's stored form already reads `0 × 40`,
`0 × 100`, `0 × 300`, `0 × 1200` — the level-0 values — which is why a level-0 round prints nothing over
it.

## The dance

`initBonusEnding` loads ten performers into sprite slots 0–9 from `dancerSprites()` (see
[`sprite-scenes.md`](sprite-scenes.md)) and gives each its own animation period, written to both halves
of the slot's `animCounter` / `animReload` pair. Because the ten periods differ, the performers never
fall into step.

How many are visible depends on the round's starting garbage height:

| Start height | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| Visible | 1 | 2 | 3 | 4 | 5 | **10** |

The jingle rises with the height too — height 0 gets `MusicId::TYPE_B_JINGLE_1`, height 5 gets `_6`.

`dancers` then counts each visible slot's `animCounter` down once per eligible frame; when one reaches
zero it reloads and flips that performer's sprite to its other frame by toggling the low bit of the
sprite id. Slot 6, the jumping cossack, also moves between two Y positions as he flips; the other nine
flip in place.

## Gotchas

- **The line count both endings write is 25 in decimal.** The original stores it packed-decimal as the
  byte `$25`. Reading that as hexadecimal puts 37 on the screen and nothing else goes wrong, which is
  what makes it easy to miss.
- **The screen loader here arms the wipe after the copy; `fillPlayingFieldAndWipe` arms before.** They
  are separate helpers on purpose. Do not merge them — see [`gameplay.md`](gameplay.md) for the callers
  that depend on the other order.
- **The default music query ends the dance rather than holding it.** That is the safe direction: the
  opposite default animates forever. If you change it, the round can no longer finish without audio.
- **The frame where the timer reads exactly 20 is an early return with no body.** The original redraws
  the performers on that frame and does nothing else, so the branch is real even though the port's half
  of it is empty.
- **A slot's animation counter at zero wraps to 255 rather than firing.** The original's decrement
  behaves the same way; a performer whose counter is somehow zeroed goes quiet for 255 frames.

## What to change where

| To change | Edit |
|---|---|
| How long the scoreboard holds before the count-up | `kScoreboardHoldFrames` in `src/systems/type_b_ending.cpp` |
| How long the dance layout holds before moving | `kDanceStartFrames` in the same file |
| How fast each performer moves | `kDancerAnimationPeriods` in the same file |
| Which slots use the second object palette | `kSecondPaletteSlots` in the same file |
| How high the cossack jumps | `kCossackUpY` / `kCossackDownY` in the same file |
| How many performers a height reveals | `visibleDancerCount` in the same file |
| Where a score row is printed | `kScoreboardRows` in the same file |
| What either screen looks like | the tilemap tables — see [`tilemaps.md`](tilemaps.md) |
| Which performers appear and where | `kDancerSprites` — see [`sprite-scenes.md`](sprite-scenes.md) |
