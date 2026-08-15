# Piece randomizer

**Date:** 2026-08-15
**Status:** Delivered — the draw core and `pickRandomPiece` selection. The second call site
(`NextPiece.randomChoice`, the solo per-piece draw) reuses this same draw core when the piece system
lands; it is recorded in the contract, not yet ported.

## Concept

Every random piece the game spawns comes from one mechanism: read the console's free-running divider
register (`rDIV`, incrementing independently of the running program), fold that byte into a piece
candidate, and reject a candidate that would repeat the recent piece — retrying up to three times.
This feature ports that mechanism: a draw core hosted on the engine's virtual machine, and the
native selection logic around it. The behavioral authority is
[`../contracts/piece-random.md`](../contracts/piece-random.md).

## Design decisions

**The draw core runs on the VM; the selection is native.** The fold is a short routine — read the
divider, fold the byte to a piece kind. It is transcribed to SM83 assembly (`src/vm/random.asm`) and
hosted on the engine's virtual machine. The rejection loop and the piece-pipeline bookkeeping are
ordinary C++ (`pickRandomPiece`).

The split is not arbitrary. The fold loop runs a data-dependent number of iterations, and the
divider keeps incrementing while they run — so a retry within one call reads a *different* divider
byte than its predecessor. That intra-call advancement is load-bearing: it is where a rejected
candidate gets fresh entropy instead of re-drawing itself. Running the fold on the machine
reproduces it by construction, because the machine burns the same cycles the original hardware does.

- **Rejected — a native fold over a bare divider read.** Reading the divider once natively and
  folding in C++ would freeze the divider within a call. Every retry would re-draw the same
  candidate, be rejected again, and fall through to the unconditional third-try accept every time,
  visibly changing the piece distribution.
- **Rejected — hosting the whole selection routine on the VM.** The original `PickRandomPiece` reads
  and writes two pipeline bytes that live in game state. Hosting the whole routine would mean
  shadowing those bytes into VM memory and plumbing them back each call, and would need a second
  assembly routine for the near-identical `NextPiece` variant — all to move a few tens of cycles of
  glue onto the machine. The glue between draws is well under one divider increment, so running it
  natively changes nothing observable.

The port is not cycle-exact at whole-game scale, and does not need to be: across a whole game the
entropy comes from *when* the player's inputs cause draws to happen, exactly as on hardware. The
fidelity target is the fold algorithm plus the intra-call feedback structure.

**One shared draw core for both sites.** `PickRandomPiece` (two-player start) and
`NextPiece.randomChoice` (solo per-piece draw) fold the divider identically but keep different
pipeline state. Rather than a shared "selection" abstraction, the shared piece is the fold — the
assembly routine — and each site supplies its own reference and bookkeeping in native code. The
solo-draw site is recorded in the contract (§4b) and ported with the piece system.

**The routine is baked into the binary.** The assembly is assembled at build time and embedded, so
no assembly file needs to ship. This is the port's own asset — nothing derived from the original ROM
lives in it.

## Implementation details

Files:

- `src/vm/random.asm` — the draw core: read the divider, fold the byte, leave the candidate in
  register A. Two byte-equivalent adaptations from the original body are noted in the file header
  (the leaf `ret` in place of a fall-through, and `28` for the original's `7 * 4` since the engine
  assembler has no multiply operator — the value is pinned in the tests).
- `src/vm/piece_random.{h,cpp}` — `kirpich::vm`:
  - `registerPieceRandom(retropp::Vm&)` — registers the draw core and returns it as a callable byte
    source yielding one of `{0, 4, 8, 12, 16, 20, 24}`.
  - `pickRandomPiece(const Routine&, GameFlowState&)` — draws up to three candidates, rejects a
    repeat of the temp-preview kind (the third draw accepted unconditionally), advances the piece
    pipeline (`nextPreviewPiece` takes the accepted candidate, `tempPreviewPiece` takes the old
    next-preview), and returns the old next-preview — a one-stage pipeline.
- `tests/test_piece_random.cpp` — six behavioral cases: the draw domain, the fold relation against
  hand-traced vectors, determinism across reset, the divider feeding the draw, the intra-call
  advancement quirk, and the `pickRandomPiece` pipeline and auto-accept.

Wiring: `src/CMakeLists.txt` adds `vm/piece_random.cpp` to the port library and applies the routine
embed scan so the assembly is baked into the binary.

The caller advances the virtual machine one tick's worth of cycles each sim tick so the divider
free-runs between draws; this is the piece system's responsibility when it consumes the draw.

## Open questions / future work

- **`NextPiece.randomChoice`** — the solo per-piece draw. Same fold, but the rejection reference is
  the visible preview piece and the accept path writes one field with no pipeline return. It reuses
  this feature's draw core as native code and lands with the piece system. Recorded in the contract
  (§4b).
- **The two-player warm-up and piece-list fill** — the two-player start discards three draws to
  prime the pipeline, then fills a 256-entry shared piece list. That caller lands with the
  multiplayer start path. Recorded in the contract (§4a).
- **No hardware trace.** The verification substitutes determinism, contract vectors, and the quirk
  check for a side-by-side trace against the original, which is not possible before the main loop
  exists. Recorded in the contract (§6); revisited if a hardware trace becomes available.
