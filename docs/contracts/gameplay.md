# Gameplay session — behavioral contract

The states a round of Tetris passes through: the init that sets a game up, the per-frame loop that
plays it, the pause the player can interrupt it with, and the game-over chain that tears it down.
Derived from `tetris.asm`; every behaviour below carries its source line.

Scope: the solo session. Two-player rounds run their own gameplay state and are specified with the
multiplayer work; the pause routines here are shared with it and are noted where they fork.

---

## 1. The states

| State | Handler | Source | Role |
|---|---|---|---|
| `$0A` | `initGame` | `:4124–4238` | Set up a round — both game types, and the attract demo |
| `$00` | `normalGameplay` | `:4406–4421` | One frame of play |
| `$01` | `initGameOver` | `:4577–4593` | Start the game-over curtain |
| `$0D` | `gameOverCurtain` | `:4917–4970` | Paint the game-over screen, pick the ending |
| `$04` | `gameOverScreen` | `:4595–4615` | Wait for the player, return to the menu |
| `$0B` | `initTypeBScoreboard` | `:4708–4716` | Re-arm the Type B results count-up |
| `$0C` | `state0CUnknown` | `:4909–4915` | Unreachable (§8) |

---

## 2. One init serves every mode

`$0A` is entered from four places: Type A level selection (`:3354`), Type B level selection
(`:3454`), Type B start-height selection (`:3518`), and the attract-demo launch (`:615`). It forks
internally on the game type (`:4143`) rather than being duplicated per mode, so a Type A round, a
Type B round, and a demo all share this init and the `$00` loop that follows it. A two-player round
does not — it runs `$1A`.

### 2.1 Sequence

1. **Clear the entry block** (`:4126–4132`) — the preview sprite object, the piece lock stage, the
   blink phase, the top-score pointer's high byte, and the line count's high byte.

   Two of those bytes have no port field. The lock-stage shadow at `$FF9B` is written by the sprite
   path and never read, so clearing it has no observable effect and the port omits it. The line
   count is a two-byte packed-decimal value in the original and the routine clears only its high
   byte; the port's line count is a single decimal integer, so it clears the whole field. Nothing
   reads the line count between here and step 6, which overwrites it.

2. **Clear the board** (`:4134–4135`) — fill the visible field with spaces, then clear the two rows
   below it.

3. **Clear the score and statistics** (`:4136`).

4. **Zero the wipe step** (`:4137–4138`) — see §3.

5. **Clear the object buffer** (`:4139`).

6. **Fork on game type** (`:4140–4194`):

   | | Type A | Type B |
   |---|---|---|
   | Starting level from | the Type A level choice | the Type B level choice |
   | Line count starts at | 0 | 25 |
   | Initial drop timer | from the gravity table | 52 (`:4212–4213`) |

7. **Load the gravity period** for the starting level (`:4196`), honouring heart mode.

8. **Hide the preview** if the player turned it off (`:4197–4201`).

9. **Fill the piece pipeline** — three draws (`:4203–4205`). The pipeline is one stage deep and each
   draw returns the *previous* preview, so three calls are what it takes to leave both the active
   piece and the visible preview valid.

10. **Clear the completed-row count** (`:4207–4208`).

11. **Type B only:** if the chosen start height is non-zero, fill that many rows of garbage
    (`:4219–4232`) — from the fixed demo table during a demo (`:4225`), randomly otherwise
    (`:4232`).

12. **Enter play** (`:4236–4237`).

---

## 3. The board fill arms a wipe, and the init immediately disarms it

`FillPlayingFieldAndWipe` (`:5039–5043`) sets the wipe step to 2 before filling the field, so the
row-by-row wipe animation runs after it. Three callers use it and they do not agree on whether they
want that:

| Caller | Fill tile | Wipe runs? |
|---|---|---|
| `$0A` init (`:4134`) | space | **No** — the init zeroes the step immediately after (`:4137–4138`) |
| `$01` game-over init (`:4588`) | `$87` | Yes |
| `$0D` curtain (`:4934`) | space | Yes |

The ordering is the contract. The arm-then-disarm on the init path is not redundant code to be
folded away: the same helper is shared, and the two other callers depend on the arm.

`$87` is a tile no tetromino uses, which is what makes the game-over curtain visually distinct from
an ordinary field clear.

---

## 4. The gameplay frame

`$00` runs twelve steps in a fixed order (`:4406–4421`):

| # | Step | Source |
|---|---|---|
| 1 | Handle Start / Select — pause, preview toggle, soft-reset chord | `:4407` |
| 2 | If paused, stop here | `:4408–4410` |
| 3 | Check whether the demo has ended | `:4411` |
| 4 | Substitute the demo's recorded input | `:4412` |
| 5 | Record input (inert unless recording is enabled) | `:4413` |
| 6 | Rotate / shift the active piece | `:4414` |
| 7 | Apply gravity and soft drop | `:4415` |
| 8 | Scan for completed rows | `:4416` |
| 9 | Lock the piece into the board | `:4417` |
| 10 | Compact the board after a clear | `:4418` |
| 11 | Award the line-clear score | `:4419` |
| 12 | Restore the player's real input | `:4420` |

The order is observable: the scan runs *before* the lock, so a piece is scanned in the position it
had on entry to the frame, and the compaction runs before the award, so the award sees the tallies
the compaction produced.

Steps 3, 4, 5 and 12 belong to demo playback and recording; they do nothing during ordinary play
(each returns immediately when no demo is active — `:735–737`, `:774–776`, `:825–827`, `:864–866`).

---

## 5. Pause

`HandleStartSelect` (`:4440–4512`) is shared between the solo loop (`:4407`) and the two-player game
(`:1681`).

**The soft-reset chord is checked twice per frame.** The frame dispatcher already tests
Start+Select+B+A each tick, and this routine tests it again (`:4441–4444`). The disassembly notes the
redundancy. Both checks are preserved.

**A demo suppresses everything below.** With a demo running, the routine returns before any pause or
preview handling (`:4445–4447`) — the recorded input cannot pause the game.

**Select toggles the preview** (`:4423–4438`): flip the hide flag, and hide or show the preview
sprite object accordingly.

**Select keeps working while the game is paused, and that is preserved.** Nothing between
`HandleStartSelect` and `handleSelect` tests the pause flag (`:4448–4450`) — the only gates on the
path are the soft-reset chord and the demo. Pausing selects the other background map (`:4461`) and
leaves object display alone, so a preview brought back while paused is drawn over the paused screen,
which has no playing field for it to sit in. The pause hides both pieces on the way in (`:4477–4481`),
so this is the one object that can reappear there.

Pinned by `SelectStillTogglesThePreviewWhilePaused` in `tests/test_readouts.cpp`. Do not add a pause
gate to `handleSelect`.

**Start pauses or unpauses.** Solo (`:4454–4494`): flip the pause flag; on pause, send the driver the
pause command and hide both piece sprites; on unpause, send the unpause command and restore the
preview only if the player has not hidden it. Two-player (`:4496–4512`): only the master may pause;
it saves the serial buffers across the pause.

**The two-player unpause is a protocol** (`:4515–4559`), and it has two oddities that are preserved
as written.

The master takes the short path: pressing Start again clears the pause flag and jumps straight into
the shared unpause (`:4500–4503`), which restores the saved serial buffers, resumes the music, and
clears the flag. It does not wait for the protocol.

The slave runs `HandlePausedMultiplayer`, where both oddities live:

- **The serial-flag test can never be taken** (`:4519–4520`). The routine loads the transfer flag and
  branches on zero — but loading a byte does not touch the condition flags, so the branch actually
  tests the pause check two lines above, which has already returned when it was zero. The routine
  therefore proceeds on every frame regardless of the flag, and clears the flag unconditionally.
- **The unpause test reads inverted against the command it names** (`:4536–4537`). Reading `$94` — the
  value the master sends to unpause — makes the slave return still paused; any other value makes it
  unpause. Whether the buffer naming in the disassembly is itself reversed is a question for the
  serial protocol work; the behaviour is carried exactly as the code has it.

### 5.1 The caller-skip

`HandlePausedMultiplayer` discards its caller's return address (`:4529`, `:4557`) so that returning
from it skips the rest of the caller. The port cannot express that directly; the routine reports it
instead, and the two-player handler returns when it is told to. This is the one construct in this
unit that could not be carried across as written.

### 5.2 The pause command

Pausing and unpausing write a command byte the sound driver reads and clears each tick: `1` to
pause, `2` to unpause. Five sites write it — `:4463` and `:4506` pause, `:4489` and `:4544` unpause,
and `:1773` from the two-player round. It behaves exactly like the four sound cues: the game writes,
the driver drains, a stale value never re-fires.

---

## 6. Game over

**`$01`** (`:4577–4593`) — hide both piece sprites, clear the lock stage and blink phase, clear the
line-clear list, fill the field with `$87` (which starts the wipe, §3), set a 70-frame timer, and
advance to the curtain.

**`$0D`** (`:4917–4970`) — wait for the timer, then cue the game-over music.

- **Two-player** (`:4923–4930`): set a 63-frame timer, flag the serial byte, advance to the
  two-player end jingle.
- **Solo** (`:4932–4956`): fill the field with spaces, print the game-over frame and the "please try
  again" message into the board, then pick the ending.

**The ending fork is Type A only** (`:4943–4956`). The original compares the top two digits of the
score against three thresholds and picks a rocket accordingly:

| Score | Rocket |
|---|---|
| ≥ 200 000 | large |
| ≥ 150 000 | medium |
| ≥ 100 000 | small |
| below | none — go to the game-over screen |

On a match: stage the rocket, set a 144-frame timer, and advance to the bonus-ending scene.
Otherwise advance to the game-over screen.

**`$04`** (`:4595–4615`) — wait for A or Start, zero the wipe step, then return to whichever menu the
round came from: the two-player difficulty screen, the Type A difficulty screen, or the Type B one.

---

## 7. The Type B results re-arm

`$0B` (`:4708–4716`) waits for the timer, sets the count-up phase to 1, and reloads the timer with 5.
It is the beat that drives each unit of the Type B results tally; the tally itself is specified with
the scoring work.

---

## 8. `$0C` cannot be reached

`GameState_0C` (`:4909–4915`) waits for any button and advances to the first bonus-ending scene. No
code path reaches it.

The dispatch index is written at 60 sites. Every one of them loads an immediate value or zeroes the
register first; the eight reads of the index (`:72`, `:420`, `:1704`, `:4996`, `:5628`, `:5756`,
`:5815`, `:5826`) are all comparisons or the dispatch itself, so the index is never modified in
place. The value `$0C` appears in executable code only twice — as a loop count (`:550`) and as an
address-nibble comparison (`:4386`) — and its decimal spelling appears nowhere. No instruction can
place `$0C` into the dispatch index.

The handler is carried anyway: it is a real entry in the dispatch table, and the port reproduces the
table as the original built it.

---

## 9. What this unit does not do

Everything below is display work, specified with the renderer:

- Turning the screen off and on (`:4125`, `:4234`, `:4454`, `:4461`, `:4487`).
- Loading the background tilemaps (`:4154`, `:4157`) and the level and heart digits (`:4162–4175`,
  `:4215–4218`).
- The paused screen's tilemap swap and its copy of the line count (`:4464–4476`), the "pause" text
  (`:4561–4572`), and the reprint on unpause (`:4547–4554`).
- Compiling the piece sprites into the display (`:4206`, `:4482–4483`, `:4581–4582`).

The distinction that decides each case: writes to the board are simulation and are carried here;
writes to video memory are display and are not. The game-over text is a board write (`:4938`,
`:4942`) and is therefore carried — the tiles it prints come from the static tilemaps.

Filling garbage is specified with the Type B work; this unit calls it and passes the row count and
whether the fixed demo table is used.
