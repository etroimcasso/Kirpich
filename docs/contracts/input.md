# Contract — Input layer

**Source of truth:** `tetris.asm` (kaspermeerts/tetris, DMG) at `b95c668`.
**Primary routine:** `ReadJoypad` (`tetris.asm:6527–6554`).
**Related sites:** `RotateAndShiftPiece` (`tetris.asm:5965–6028`), name entry (`tetris.asm:3984–4058`),
`DemoSimulateJoypad` (`tetris.asm:788–816`), `ReadJoypadAndBlinkCursor` (`tetris.asm:3597–3608`).

This document is the behavioral authority the port's tests are written against. It describes what
the original game does; the port reproduces that observable behavior over the engine's action-input
system.

---

## 1. The per-frame snapshot

The original polls the joypad exactly once per frame. `MainLoop` calls `ReadJoypad`
(`tetris.asm:387`), which reads the hardware register (four reads of the d-pad nibble and ten of the
button nibble, to mitigate switch bounce), inverts to make pressed bits high, packs all eight
buttons into one byte, and stores two bytes every consumer reads:

- **`hJoyHeld`** (`$FF80`) — the buttons currently down this frame.
- **`hJoyPressed`** (`$FF81`) — the buttons that went down *this* frame (the rising edge).

Every input the game consumes reads that held/pressed pair; the sixteen consumer sites belong to
later systems (the state dispatcher and its soft-reset chord, the piece system, the menus, the demo
playback, the end-of-game screens, name entry). This layer delivers the pair.

The two HRAM bytes are engine mechanism, not game state: the game-flow state contract already assigns
`$FF80–$FF81` to "the per-tick input snapshot, delivered by the engine input bridge"
([`game-state-machine-state.md`](game-state-machine-state.md)). This layer is that bridge. It owns
the previous-held byte as its own mechanism state — it is not a field of any game-state struct.

## 2. The edge relation

`ReadJoypad` derives the pressed byte from the held byte and the previous frame's held byte
(`tetris.asm:6546–6549`):

```
    ldh a, [hJoyHeld]   ; a ← previously-held
    xor c               ; c ← newly-read held
    and c
    ldh [hJoyPressed], a
    ld  a, c
    ldh [hJoyHeld], a   ; previously-held ← newly-read held
```

`(prev xor new) and new` keeps exactly the bits that are set in `new` and clear in `prev`:

```
pressed = held & ~previouslyHeld
```

The port applies this same relation per action bit and then stores the new held as the previous. On
the first sample after a reset the previous is empty, so every held action reads as newly pressed —
the correct edge for input already down when sampling begins.

## 3. Levels only — a sub-tick tap is dropped

The original samples the button *levels* once per frame. A tap that goes down and back up between two
frames is never observed: `hJoyHeld` reads low on both frames, so no edge is ever produced. Behavior
preservation requires this: the bridge samples **held levels only** and derives the edge itself,
rather than reading the engine's own just-pressed signal.

The engine's just-pressed signal carries a stronger, deliberately different guarantee — a press is
*never* dropped, even a tap shorter than one tick registers exactly one press (the run loop keeps a
per-tick union of every frame's held state). That property is correct for a modern game but is *not*
what the original does, so this bridge does not use it. Sampling held levels and deriving the edge
also makes the live path and the demo path symmetric (§5): both feed a held set through the same call.

## 4. Key repeat (DAS)

One countdown byte, `hKeyRepeatTimer` (`$FFAA`, carried as the game-flow state's `keyRepeatTimer`),
drives auto-repeat at two sites that share one core:

- **press** → fire immediately and arm the long initial delay (`timer = 23`).
- **held** → decrement the timer; on reaching zero, fire and reload the short repeat interval
  (`timer = 9`); otherwise do not fire.
- **neither** → do not fire.

The shared core is `keyRepeatFire(timer, pressed, held)`; it returns `true` on the frames the action
fires. The two constants are line-anchored and test-pinned:

| Constant | Value | Source |
|---|---|---|
| initial delay | `23` | `tetris.asm:5974`, `:6008`, `:3989` (`$17`) |
| repeat rate | `9` | `tetris.asm:5982`, `:6016`, `:4024`, `:4056` |
| blocked retry | `1` | `tetris.asm:6001` |

### 4a. Wraparound is contract

If the held path runs with the timer already at `0` — possible when a site is entered with a
direction held and a stale timer — the decrement (`dec a`) wraps to `$FF`, a 255-frame delay before
the next fire. An unsigned-byte decrement reproduces this exactly, so the port preserves it verbatim.

### 4b. Site-specific behaviors (recorded here; ported with their owning systems)

The two sites add their own behavior on top of the shared core. These are recorded here and
implemented where the owning system lands:

- **Piece shift** (`RotateAndShiftPiece`, `tetris.asm:5965–6028`), ported with the piece system:
  - **Idle re-arm.** When no shift button is pressed or held, the fall-through path stores the still-
    loaded `23` back into the timer every frame (`:6006–6011` → `.out` at `:6002–6003`), so the delay
    is always fully armed when a shift finally begins.
  - **Wall-charge retry.** When a shift is blocked by a collision, the site parks the timer at `1`
    (`:6001`) so the next frame retries immediately.
  - **Right-over-left priority** (`:5973` tested before `:6007`).
- **Name entry** (`tetris.asm:3984–4058`), ported with the name-entry screen:
  - **No idle re-arm.** The no-input path returns with the timer untouched (`:4003`).
  - **Up-over-down priority** (`:3990–3997` test order).

The core owns none of these; it takes the timer by reference and each site supplies the surrounding
behavior.

## 5. The demo seam

The attract-mode demo substitutes recorded input for live input without touching any consumer.
`DemoSimulateJoypad` (`tetris.asm:788–816`) reads the next held byte from the demo timeline, derives
pressed with the **same** edge relation against the demo's own previous-held byte (`:794–796`), saves
the player's real held byte to `hSavedJoyHeld`, and overwrites `hJoyHeld` with the demo's held byte
(`:812–815`). The substitution happens at the held-set level; the edge is always derived, never
substituted.

The port reproduces this with one call: live play feeds the engine-sampled held set into
`InputSystem::sample`, and the demo playback feeds the recorded timeline's held set into the *same*
`sample`. Because the edge is derived inside that one call, demo input slots in without any consumer
distinguishing it from live input.

## 6. `ReadJoypadAndBlinkCursor` is not input

`ReadJoypadAndBlinkCursor` (`tetris.asm:3597–3608`) does not poll the joypad; it *reads* the already-
sampled `hJoyPressed` for its callers and blinks a cursor tile on a frame timer. It is a menu helper
and is ported with the menu system, not here.

## 7. Reset

The only state this layer holds is the previous-held set inside `InputSystem`. `reset()` clears it to
empty (the boot state), after which the next sample treats every held action as newly pressed. The
`keyRepeatTimer` byte lives in the game-flow state and is cleared by that struct's reset; the shared
core owns no persistent state.

## 8. The action vocabulary

Input resolves to the game's own action vocabulary (`include/kirpich/action.h`) carried in the
engine's action set — there is no port-side raw-button type. The five piece-control actions
(`MoveLeft`, `MoveRight`, `SoftDrop`, `RotateClockwise`, `RotateCounterClockwise`) are what this layer
binds and reads today; the enum gains actions as the menu, chord, and name-entry surfaces land, and
the default bindings and the held-action walk grow with it.

The default bindings map the two rotations to the pad's printed A / B (so the glyph matches the
original Game Boy button on every pad family) and to Z / X on the keyboard (the emulator convention);
movement binds to the arrows and the d-pad. Up is bound to nothing yet — soft drop is Down, and menu
navigation actions do not exist until the menus land.

---

## 9. Verification

A full side-by-side trace against the original is not possible before the main loop exists, since the
input cadence is the whole game's frame timing. In its place the port pins:

1. **The edge relation** — fresh press, sustained hold, release-and-repress, simultaneous multi-
   action sets, the sub-tick-tap drop (§3), and the first-sample-after-reset semantics.
2. **The key-repeat timeline** — the full DAS countdown (press arms 23, the 23rd held frame fires and
   reloads 9, then every 9th held frame fires), the untouched-on-idle path, the wall-charge retry, and
   the stale-zero wraparound (§4).
3. **The constants** — 23 / 9 / 1 against their cited lines.
4. **The default bindings** — every (action, source) pair, and no rows for any action beyond the five.
5. **The held-action adapter** — the per-tick engine input state read into an action set through the
   engine's documented device-free test seam.

**Known gap:** none of the above compares against a capture from real hardware or a reference
emulator; the gap is inherent to porting without the original runtime and is revisited if a hardware
trace becomes available.

---

## Tested by

`tests/test_input.cpp` — six behavioral cases: `EdgeRelation`, `KeyRepeatTimeline`, `KeyRepeatEdges`,
`ConstantsPins`, `DefaultMapRows`, `HeldActionsAdapter`. All device-free; every asserted value traced
to the lines named above.
