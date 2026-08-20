# Demo playback — behavioral contract

Reverse-derived from the disassembly. Line references are `tetris.asm` unless another file is named.
This is the test authority for `src/systems/demo.{h,cpp}`; where the code and this document disagree,
the disassembly settles it.

The state this behavior reads and writes is specified in
[`demo-state.md`](demo-state.md); the two recordings and the shared piece list are in
[`demo.md`](demo.md). This document covers only what happens at run time.

---

## 1. What a demo is

The attract demo is not a recording of a screen. It is the game playing itself: the ordinary round
init, the ordinary frame, the ordinary piece and line-clear logic, with recorded input substituted for
the player's (`769-772`).

Three consequences, and together they are the whole design:

- **The input is run-length encoded.** Each step of a recording holds a set of buttons and a frame
  count, so the cursor advances only on the frames the input changes.
- **The pieces are not random.** While a demo runs, the piece pipeline takes the shared fixed list
  rather than the randomizer (`5090-5098`), so a recording always produces the same round.
- **The player keeps the buttons.** The substitution lasts one frame beat and is undone before the
  frame ends (`863-872`), which is what lets Start end the demo and the soft-reset chord work.

---

## 2. Launching — `StartDemo` (`582-624`)

### 2.1 The alternation reads the demo that ran last

The branch at `596-599` tests the value already stored, and the store at `614` writes the new one.
Reading it as a test of the demo about to run inverts the sequence.

| Demo that ran last | Configured | Now running |
|---|---|---|
| none (cold start) | Type A | Type A |
| Type A | Type B | Type B |
| Type B | Type A | Type A |

So from power-on the sequence is Type A, Type B, Type A, … The number stored survives a return to the
title screen — the title init does not clear it (only starting a real game does, `695`) — which is what
makes the alternation continue and what shortens the attract countdown on the way back.

### 2.2 The configuration

Written unconditionally first (`583-595`):

| Field | Value |
|---|---|
| game type | Type A |
| Type A level | 9 — demos play fast |
| multiplayer | off |
| pieces played | 0 |
| the recording's held set | empty |
| frames remaining | 0 |
| cursor | rewound to the first step |

Then, for a Type B demo only (`600-611`):

| Field | Value |
|---|---|
| game type | Type B |
| Type B level | 9 |
| Type B starting height | 2 |
| pieces played | 17 |

**The Type A level stays 9 on a Type B demo.** It is written before the branch and the branch does not
undo it. Preserved.

The original points its cursor at the chosen recording's base address (`592-595`, `606-609`). The port
selects the recording from which demo is running and rewinds one index, which is the same thing said
once instead of twice.

Finally the round-init state is entered (`615-616`), and the round init then runs exactly as it does
for a player — including the starting-garbage fill, which takes the fixed demo table rather than a
generated one while a demo is running (`4222-4225`).

### 2.3 The screen it starts from

`617-623` runs the first four steps of the config screen's own load and stops: the gameplay tile set,
the config-screen backdrop into the first map, the object-buffer clear, and then the display control
byte. It does **not** go on to place the config screen's cursors, cue its music, or enter game-type
selection, all of which follow in that screen's own load (`3127-3145`).

The control byte written on the way out (`622-623`) has the background-map bit clear, so the first map
is what shows.

**The backdrop is on screen for one frame.** The round init overwrites both maps on the next frame.
That single frame is real on hardware and is reproduced rather than skipped.

---

## 3. The frame

Four routines run inside the gameplay frame, in this order (`4411-4413`, `4420`):

1. `CheckForEndOfDemo`
2. `DemoSimulateJoypad`
3. `RecordDemo`
4. — the piece, line-clear and scoring steps —
5. `RestoreDemoSavedJoypad`

**The order is load-bearing twice over.** The end check runs before the substitution, so the Start it
tests is the player's own press rather than the recording's. The restore runs after every consumer of
input, so the player's buttons survive the frame — which is what the frame dispatcher's soft-reset
chord reads on the beat after dispatch.

All four begin by returning when no demo is running.

---

## 4. Replaying — `DemoSimulateJoypad` (`773-816`)

### 4.1 Gates

- No demo running (`774-776`).
- The recording flag is at its enable value (`777-779`) — tested against that value exactly, so any
  other non-zero value does not gate replay. Compare §6.

### 4.2 The two paths

**The current step still has frames** (`780-785`): count one off, and report nothing newly pressed
(`808-810`).

**The count has run out** (`787-806`): load the next step, derive its newly-pressed set, store its held
set, arm its frame count, and advance the cursor.

The explicit clearing of the pressed set on the counting path is behavior, not tidiness: a demo's
presses land only on the frames a step loads.

### 4.3 The edge is derived against the recording's own previous step

`794-796` computes the newly-pressed set as *held now, and not held before*, where **before** is the
recording's previous held set — not the previous frame's real buttons, and not the previous tick's
snapshot.

This is the same relation the live input path uses and a different baseline, and the difference is
observable: with the recording already holding right, a step that adds a rotation reports the rotation
alone as pressed, whatever the player happens to be holding at the time. Deriving it against the
player's buttons would report presses the recording never made, and would leave the live input path's
own history holding the demo's input for the following tick.

### 4.4 The substitution

Both paths converge (`811-816`): the player's held set is parked, and the recording's takes its place
in the frame's snapshot.

### 4.5 The unreachable tail

`818-822` is dead — no branch targets it, and the upstream marks it *"Unused. Maybe an earlier attempt
at RLE?"* and *"Twice unreachable?"*. Not ported: it is unreachable code with no observable effect.

---

## 5. Ending — `CheckForEndOfDemo` (`734-767`)

Two ways out, both entering the title-screen init.

**The player presses Start** (`743-751`). Because this runs before the substitution, the press is the
player's.

**The recording runs out of pieces** (`754-766`):

| Demo | Ends as the piece count reaches |
|---|---|
| Type A | 16 — it plays pieces 0 to 15 |
| Type B | 29 — its count starts at 17 |

**The test is for equality, not "at least"** (`762-764`). A count that has stepped past its target does
not end the demo this way. Preserved.

The link-cable writes bracketing the Start test (`738-742`, `746-749`) announce the exit to a connected
second console. They have no effect on this machine's simulation and belong to the serial system.

---

## 6. Restoring — `RestoreDemoSavedJoypad` (`863-872`)

Puts the parked held set back (`870-871`), unless no demo is running (`864-866`) or the recording flag
is **any** non-zero value (`867-869`).

That last gate is written differently from the two above, which test the flag against its enable value
exactly. Both readings are the original's and both are preserved.

---

## 7. The recording path is dead

`RecordDemo` (`824-860`) is called every gameplay frame and never does anything, because the routine
that arms it is never called.

**The proof.** `StartRecordingDemo` (`627-630`) appears exactly once in the disassembly — its own
definition label — with no caller anywhere. The recording flag has exactly two writers: the title
init clearing it to zero (`526-527`), and the enable value inside that uncalled routine (`629`). So the
flag is never at its enable value, and `RecordDemo`'s gate (`828-830`) always returns.

Ported dead-but-present. Given a flag set by hand, it does what the original does: an unchanged input
extends the current step (`856-859`), and a changed one closes the step, opens a new one, and advances
the cursor (`836-851`).

**Its two stores are not ported, and that is equivalence rather than omission.** They write the closed
step through the cursor, which addresses cartridge ROM — they land nowhere on hardware. Advancing the
cursor is the whole of their effect.

---

## 8. What the running demo does to the sound

The sound driver reads the running-demo byte itself (`audio.asm:73-80`). While it is non-zero, the
driver **zeroes all four cue mailboxes** — square, wave and noise effects, and the music cue — before
it plays anything.

So a demo makes no sounds of its own: none of the effects its recorded presses would normally trigger,
and no music change. It is not silence, though. Only *new* cues are blanked; `PlayMusic` still runs, so
whatever song is already playing carries on. The title song is playing when a demo launches and nothing
stops it, so it plays on underneath.

**A consequence worth knowing, because it looks like a defect and is not.** The title-screen init cues
the title song every time it runs (`565-566`), and a demo ends by entering that init — but the
running-demo byte is still set at that point, because only starting a real game clears it (`695`). So
the driver eats that cue. The title song therefore plays on a cold start and after a real round, but
**not** on the return from a demo. Preserved: clearing the byte on the way back to the title screen
would look like tidying up and would break this.

## 9. What this unit does not do

- **The link-cable writes** in `CheckForEndOfDemo` — the serial system's.
- **The `DelayMillisecond` busy-wait** at `738` — no simulation effect.
- **The three gates other units own**, each already reading the running demo: the starting-garbage
  table (`4222`), the suppression of pause and preview-toggle (`4445`), and the deterministic piece
  choice (`5090`).
- **Drawing.** A demo produces exactly the state an ordinary round produces; the renderer draws it the
  same way.
