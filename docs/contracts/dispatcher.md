# Contract — Game-state dispatcher

**Source of truth:** `tetris.asm` (kaspermeerts/tetris, DMG) at `b95c668`.
**Primary routine:** `MainLoop` (`tetris.asm:386–417`), its jump table (`:419–477`), and `TableJump`
(`tetris.asm:15–26`, the `rst $28` vector).
**Related sites:** `Init` / `Init.softReset` (`tetris.asm:264–384`), `GameState_09` (`:3149–3150`),
`HandleStartSelect` (`:4440–4444`).

This document is the behavioral authority the port's tests are written against. It describes what the
original game does; the port reproduces that observable behavior natively.

---

## 1. One frame is one pass of `MainLoop`

Every frame, the original runs `MainLoop` (`tetris.asm:386–417`) once and returns to the top to wait
for the next frame. A pass is five beats, in this order:

| # | Beat | Source |
|---|---|---|
| 1 | Poll the joypad into the held/pressed snapshot | `:387` `call ReadJoypad` |
| 2 | Dispatch through the state table to the current state's handler | `:388` `call .dispatch` |
| 3 | Advance the sound driver | `:389` `call UpdateAudio` |
| 4 | Check the four-button soft-reset chord; on a match, reset and end the frame | `:391–394` |
| 5 | Decrement the two frame timers | `:395–405` |

The port reproduces these five beats as one per-tick call. The frame *timing* — waiting for the
frame boundary and looping back — is not part of this contract: the native run loop drives one call
per tick, so the original's loop-back (`:417` `jp MainLoop`) and its frame-boundary wait (`:411–416`)
have no native counterpart (see §7).

## 2. The dispatch is a jump table read once per frame

`.dispatch` (`:419–421`) reads the current state byte and jumps through a pointer table:

```
    ldh a, [hGameState]   ; the current state
    rst $28               ; TableJump: index the table below by a, jump to the entry
```

`TableJump` (`:15–26`) doubles the index (entries are two bytes), adds it to the table base, loads
the pointer, and jumps. The table (`:423–476`) has one entry per state.

**The index is read once, before the handler runs.** A handler that stores a new state value
transitions on the *next* frame, not the current one — the current frame already committed to the
handler selected at `:420`. The port reads the dispatch index before calling the handler, preserving
this: a handler writing a new state is dispatched on the following tick.

### The 54 states and the 55th slot

The table has 55 entries. The first 54 (`:423–476`, indices `$00`–`$35`) are state handlers,
contiguous. The 55th (`:477`, index `$36`) holds a raw address (`dw $27EA`), not a handler — it is a
dispatch over-read, not a state, and no state value ever selects it. The port's state enum has 54
enumerators and the dispatch table has 54 slots; the over-read slot is not modelled. See
[`core-enums.md`](core-enums.md) for the enum's own adjudication of that slot.

The dispatch index domain is therefore `$00`–`$35`. Every producer of a state value writes one of the
54 enumerators, so an out-of-range index is a port bug, not a game state; the port guards it with a
debug assertion.

## 3. Unported states sit in place

During incremental bring-up, most state handlers are not written yet. Each unwritten slot holds a
stub that does nothing to the game state (it logs its numeric value at debug level and returns). The
state machine therefore *sits* in an unported state rather than crashing or transitioning — the
correct behavior for bring-up, and exactly what one already-bare handler in the original does:
`GameState_09` (`:3149–3150`) is a bare `ret`, so its native form needs no special handling beyond
the default stub.

## 4. The soft-reset chord

After dispatch and the audio tick, `MainLoop` checks a four-button chord (`:391–394`):

```
    ldh a, [hJoyHeld]                              ; the buttons held THIS frame
    and a, PADF_START | PADF_SELECT | PADF_B | PADF_A
    cp  a, PADF_START | PADF_SELECT | PADF_B | PADF_A
    jp  z, Init.softReset
```

- **Held levels, not edges.** The check reads `hJoyHeld` (held), not `hJoyPressed`. A sustained chord
  matches every frame it is held, and the `jp` re-executes each of those frames.
- **Exactly these four, extras don't block.** The `and` masks the held byte to Start, Select, B, A
  before comparing, so any other buttons held at the same time are masked out and do not prevent the
  match.
- **On a match the frame ends there.** `jp z, Init.softReset` leaves `MainLoop` above the timer
  decrements (`:395` onward), so on a chord frame the timers do **not** decrement — but the dispatch
  (`:388`) and the audio tick (`:389`) already ran.

The four buttons resolve to the port's action vocabulary: Start, Select, A → `RotateClockwise`, B →
`RotateCounterClockwise` (the A/B rotation mapping is the piece-shift handler's; see
[`input.md`](input.md) §8).

The port fires a soft-reset seam when all four actions are held this tick, then ends the tick before
the timer decrements. Dispatch and the audio tick run first, exactly as above.

### The reset itself is not this beat

`MainLoop` only *detects* the chord; the reset it jumps to is `Init.softReset` (`:276–384`), which is
a separate mechanism the boot path owns. One property of that reset matters here and is recorded for
the boot path: `.softReset` enters *below* the work-RAM-bank-1 clear (`:264–274`), so **a soft reset
preserves the top-score tables; only a cold boot wipes them** (the upstream comment at `:267–268`
asks exactly this). The port's dispatcher detects the chord and fires the seam; the boot path
composes the real reset and preserves the top scores.

### A duplicate check exists downstream

A second, redundant copy of the same chord check lives inside `HandleStartSelect` (`:4440–4444`),
with the upstream comment "This is already being done in the mainloop? Bug". That copy belongs to the
pause/select handler, not to this unit, and is recorded here for that handler to port.

## 5. The timer law

On a non-chord frame, `MainLoop` decrements two frame timers (`:395–405`):

```
    ld hl, hTimer1
    ld b, 2
.decrementTimer
    ld a, [hl]
    and a
    jr z, .skip      ; a zero timer is left at zero — no wrap
    dec [hl]
.skip
    inc l            ; advance to hTimer2
    dec b
    jr nz, .decrementTimer
```

`hTimer1` (`$FFA6`) and `hTimer2` (`$FFA7`) each decrement by one per frame, and a timer already at
zero is left at zero — it does not wrap to 255. The `inc l` walks from `hTimer1` to the adjacent
`hTimer2`. The port decrements each timer if non-zero, saturating at zero.

Because dispatch (`:388`) precedes the decrement (`:395`), a handler that writes a timer value has
that value decremented the same frame: a handler storing `2` reads back `1` after the frame.

## 6. The state aggregate

Every handler reads and writes the game's mutable state. The port passes each handler one aggregate
of the ported state structs plus this tick's joypad snapshot — the game's whole in-memory image. The
handler signature is fixed here so every state handler that lands is written against it. The
aggregate holds state only: no sound-driver RAM (that lives on the virtual-machine side; see
[`audio-state.md`](audio-state.md)), no renderer, no audio device — a handler that needs one receives
it separately.

## 7. Beats that are not ported here

Three things `MainLoop` does around the five beats are not part of this unit and are recorded for
their owners:

| Mechanism | Source | Owner |
|---|---|---|
| Serial interrupt-enable rewrite when a multiplayer game is running | `:406–410` | the serial/multiplayer system |
| Frame-boundary wait and loop-back (with the upstream missing-`HALT` comment) | `:411–417` | the native run loop's frame pacing — no observable native counterpart |
| Frame-counter increment (in the VBlank handler, not `MainLoop`) | `:251–252` | the frame-tick handler |
| `Init` / `.softReset` bodies and the cold-boot vs soft-reset work-RAM distinction | `:264–384` | the boot path (installs the real soft-reset target) |
| `UpdateAudio` sound-driver tick | `audio.asm` | the audio system (installs the real audio tick) |
| Duplicate chord check | `:4440–4444` | the pause/select handler |

The port exposes the audio tick and the soft reset as seams: safe defaults until their owning systems
install the real behavior.

---

## 8. Verification

A full side-by-side trace against the original is not possible before the state handlers exist, since
the dispatcher's observable behavior is the sum of its handlers. In its place the port pins:

1. **The dispatch** — a probe in every one of the 54 slots, each state dispatching to exactly its own
   handler (full-corpus sweep), and the stub path leaving the context untouched but for the snapshot
   and the timer law.
2. **The read-once transition law** — a handler writing a new state does not run that state this
   tick and does run it next tick.
3. **The beat ordering** — sample → dispatch → audio → chord, and the handler observing this tick's
   freshly-sampled snapshot.
4. **The chord** — all four held fires and skips the timers while dispatch and audio still run; each
   three-of-four subset does not fire; extras don't block; a sustained chord fires every tick.
5. **The timer law** — each non-zero timer decrements one per tick, zero saturates, and a
   handler-written value decrements the same tick.

**Known gap:** none of the above compares against a capture from real hardware or a reference
emulator; the gap is inherent to porting without the original runtime and is revisited if a hardware
trace becomes available.

---

## Tested by

`tests/test_game_state_dispatcher.cpp` — six behavioral cases: `DispatchSweep`,
`TransitionSemantics`, `BeatOrdering`, `ChordVectors`, `TimerLaw`, `NewActionRows`. All device-free;
every asserted ordering and value traced to the lines named above.
