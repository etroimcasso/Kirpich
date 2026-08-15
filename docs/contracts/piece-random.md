# Contract — piece randomizer

**Source of truth:** `tetris.asm` (kaspermeerts/tetris, DMG) at `b95c668`.
**Primary routine:** `PickRandomPiece` (`tetris.asm:1565–1606`).
**Sibling site:** `NextPiece.randomChoice` (`tetris.asm:5116–5157`) — recorded here, ported later.

This document is the behavioral authority the port's tests are written against. It describes what
the original game does; the port reproduces that observable behavior.

---

## 1. What the randomizer is

Every random piece the game spawns comes from one mechanism: read the console's free-running
divider register (`rDIV` at `$FF04`, incrementing at 16384 Hz independent of the running program),
fold that byte into a candidate piece, and reject a candidate that would repeat the recent piece —
retrying up to three times. Two sites run this mechanism:

- `PickRandomPiece` — the shared routine. Called only from the two-player start path
  (`GameState_16`, `tetris.asm:995–1004`).
- `NextPiece.randomChoice` — an inline near-copy used for the per-piece draw during play.

The fold is identical at both sites; the rejection bookkeeping differs (§4).

The demo (attract-mode) path is **not** random: `GameState_24` copies a fixed piece list into the
piece buffer instead of drawing (recorded in the demo-data contract). The randomizer is never
consulted there.

---

## 2. The fold — divider byte to piece kind

`tetris.asm:1573–1586` (byte-identical to `:5119–5132`):

```
    ldh a, [rDIV]   ; b ← the divider byte, 0..255
    ld  b, a
.wrap
    xor a           ; a ← 0
.loop
    dec b
    jr z, .done     ; when b reaches 0, a is the candidate
    inc a
    inc a
    inc a
    inc a           ; a += 4
    cp a, 28        ; 7 kinds x 4 == 28
    jr z, .wrap     ; a wrapped past the last kind → back to 0
    jr .loop
.done
```

The loop decrements the divider byte and adds 4 to an accumulator once per decrement, wrapping the
accumulator back to 0 when it reaches 28. When the byte reaches 0 the accumulator is the result.

**Result domain:** `{0, 4, 8, 12, 16, 20, 24}` — a piece byte with the low two bits (rotation) clear
and the kind in the upper bits, i.e. `kind * 4`, orientation 0. Kind ranges over the seven
tetrominoes (see `include/kirpich/piece.h`).

**Closed form** (derivation, not a substitute for the loop): treating the initial byte `b` as a
value in `1..256` (a read of 0 decrements to 255 first, so it behaves as 256), the candidate is
`4 * ((b - 1) mod 7)`. Worked points, used as test vectors:

| divider byte `b` | +4 steps | candidate |
|---|---|---|
| 1 | 0 | 0 |
| 2 | 1 | 4 |
| 7 | 6 | 24 |
| 8 | 7 (wraps) | 0 |
| 9 | 8 | 4 |
| 0 (behaves as 256) | 255 | 12 |

**Upstream note carried verbatim:** the four single `inc a` instructions (rather than one `add a, 4`)
are annotated in the source as possibly "a way to waste time to allow the DIV register to
increment." That side effect is load-bearing — see §3.

---

## 3. The load-bearing quirk — the divider advances *within* a call

The fold loop runs a data-dependent number of iterations (up to ~256, one per unit of the divider
byte). Those iterations consume real CPU cycles, during which `rDIV` keeps incrementing. So when the
routine rejects a candidate and reads `rDIV` again for the retry (§4), the second read observes a
**different** byte than the first — the divider has moved on while the first fold ran.

This is why the fold must run on a cycle-executing machine rather than as a plain native
calculation. If the divider byte were sampled by a bare read that did not advance during the fold,
every retry within one call would re-read the same byte, produce the same candidate, be rejected
again, and exhaust to the unconditional third-try accept every time — visibly changing the piece
distribution. The port runs the fold on the engine's SM83 virtual machine so the divider advances
across the fold exactly as it does on hardware.

The port is not cycle-exact at whole-game scale, and does not need to be: across the whole game the
entropy comes from *when* the player's inputs cause draws to happen, the same as on hardware. The
fidelity target is the fold algorithm plus this intra-call feedback structure.

---

## 4. Rejection and the pipeline — the two sites

Both sites draw a candidate (§2), then decide whether to keep it, retrying up to three times. Let:

- `cand` — the freshly folded candidate (low two bits clear).
- `next` — the current "next preview" piece byte.
- `c` — the rejection reference, masked to its kind bits (`& ~3`).

**Try budget and the auto-accept.** The try counter starts at 3 and is decremented **before** the
rejection test. On the third draw the counter reaches zero and the candidate is accepted
unconditionally — the rejection test is skipped. So a call performs at most three draws and always
returns a piece.

**Rejection test** (`tetris.asm:1594–1598`, identically `:5150–5154`):

```
reject if  ((next | cand | c) & ~3) == c
```

i.e. bitwise-OR the next-preview, the candidate, and the reference kind; mask to kind bits; reject
the candidate if the result still equals the reference kind. On rejection the routine loops back and
draws again (reading a fresh, advanced `rDIV` per §3).

### 4a. `PickRandomPiece` (`tetris.asm:1565–1606`) — the ported site

- Reference `c` = **temp-preview** piece kind (`hTempPreviewPiece & ~3`), read at `:1568–1570`.
- On accept (`:1599–1606`):
  - `next-preview ← cand`
  - `temp-preview ← old next-preview`
  - **returns the old next-preview** in `A` (the value stored into temp-preview).

The return is the load-bearing pipeline detail: the routine does **not** return the candidate it just
drew. It returns the *previous* next-preview and shifts the freshly drawn candidate into the
next-preview slot — a one-stage pipeline. The caller consumes a piece that was drawn one call
earlier.

**Caller — `GameState_16` MASTER path (`tetris.asm:995–1004`):**

```
    call PickRandomPiece   ; three warm-up calls
    call PickRandomPiece   ; return values discarded — they prime the pipeline
    call PickRandomPiece
    ld b, 0                ; 256 entries (b wraps 0 -> 255 -> ... -> 0)
    ld hl, wPieceList
.loop
    call PickRandomPiece
    ldi [hl], a            ; store the returned (previous) preview
    dec b
    jr nz, .loop
```

The three leading calls discard their results; they exist only to advance the pipeline so the first
stored entry is a fully-drawn piece. The 256-entry fill then stores each call's returned byte. This
list is shared with the second player over the link cable; both machines walk the same list. The
warm-up count (3) and fill count (256) are the caller's, recorded here; they are exercised where
`GameState_16` is ported.

### 4b. `NextPiece.randomChoice` (`tetris.asm:5116–5157`) — recorded, ported later

Same fold (§2), same try budget and auto-accept, but:

- Reference `c` = **preview** piece kind (`[$C210 + 3] & ~3`), read at `:5086–5089` — the visible
  preview piece, not the temp-preview.
- On accept (`:5155–5157`): writes **next-preview only**; it does not write temp-preview and does not
  return the old preview. The promotion of next-preview into the visible preview happens afterward in
  `.setupPiece` (`:5158+`).
- Demo and multiplayer are gated out ahead of the draw (`:5090–5095`): both route to
  `.deterministicChoice`, which walks the shared piece list by an index instead of drawing. Only
  solo play reaches `.randomChoice`.

The differences from 4a are: the reference source (preview vs. temp-preview), and the accept path
(one field write, no return-of-previous). The fold and the intra-call quirk are the same, which is
why the port hosts one shared fold routine and lets each site supply its own reference and
bookkeeping.

---

## 5. Reset

There is no piece-randomizer state that persists in port-owned storage: the pipeline slots
(next-preview, temp-preview) live in the game-flow state and are cleared by its reset. The virtual
machine's own reset clears the divider and any machine state. Nothing else needs a reset hook.

---

## 6. Verification — the substitute for a two-baseline trace

A full side-by-side trace of the original against the port is impossible before the main loop
exists, because the draw cadence is the whole game's input timing. In its place the port pins:

1. **Determinism** — a reset machine advanced on a fixed schedule yields an identical draw sequence
   across repeated runs and across platforms. The expected sequence is captured once and checked
   thereafter, including in cross-platform CI.
2. **Contract vectors** — the fold (§2) and the rejection test (§4) are checked against the
   hand-traced values in this document (candidate for a given divider byte; accept/reject and the
   field mutations for a given `next`/`c`/`cand`), including the third-draw auto-accept.
3. **The quirk** — within one call whose first candidate is rejected, the retry's divider read
   differs from its predecessor (§3), proving the intra-call advancement survives the port.

**Known gap:** none of the above compares against a capture from real hardware or a reference
emulator. The gap is inherent to porting without the original runtime available; it is recorded here
and revisited if a hardware trace becomes available.
