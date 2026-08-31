# Piece randomizer

How the game draws random pieces, and what to edit to change it. The behavioral specification —
what the original game does, line by line — is in
[`../contracts/piece-random.md`](../contracts/piece-random.md); the design rationale is in
[`../features/piece-random.md`](../features/piece-random.md).

## Where it lives

| File | Holds |
|---|---|
| `src/vm/random.asm` | The draw core: SM83 assembly that reads the free-running divider (`rDIV`) and folds the byte into a piece candidate, leaving it in register A. |
| `src/vm/piece_random.h` / `.cpp` | The `kirpich::vm` surface — `registerPieceRandom` and `pickRandomPiece`. |
| `tests/test_piece_random.cpp` | The behavioral tests. |

The randomizer runs on the engine's virtual machine — Polyrhythm's Conductor layer — which hosts the
handful of routines
that need a cycle-executing CPU. The draw core is one of them; everything else is ordinary C++.

## The surface

```cpp
namespace kirpich::vm {

// Register the draw core on `vm` and return a callable byte source. Each call reads the divider and
// folds it; the returned byte is one of {0, 4, 8, 12, 16, 20, 24} — a piece kind times 4,
// orientation 0.
retropp::Routine<std::uint8_t()> registerPieceRandom(retropp::Vm& vm);

// Draw a piece (up to three tries, rejecting a repeat, third draw accepted unconditionally),
// advance the piece pipeline in `flow`, and return the piece to play next.
Piece pickRandomPiece(const retropp::Routine<std::uint8_t()>& draw, GameFlowState& flow);

}
```

**Using it.** Construct a Game-Boy-timed VM, register the draw once, then call `pickRandomPiece` each
time a piece is needed. Advance the VM's clock one tick's worth of cycles every sim tick so the
divider keeps moving between draws:

```cpp
retropp::Vm vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
const auto draw = kirpich::vm::registerPieceRandom(vm);

// once per sim tick:
vm.advanceClock(retropp::TimingProfile::GameBoy.cpuCyclesPerTick());

// when a piece is needed:
const Piece next = kirpich::vm::pickRandomPiece(draw, gameFlow);
```

Without the per-tick `advanceClock`, the divider freezes between calls and the draw degenerates into
a counter.

`pickRandomPiece` reads and writes `flow.nextPreviewPiece` and `flow.tempPreviewPiece`. It returns
the *previous* next-preview, not the piece it just drew — the draw enters the pipeline one call
before it comes out. The value returned is what a two-player game stores into the shared piece list.

## Changing behavior

- **The fold** (which divider byte maps to which piece) is `src/vm/random.asm`. It is assembled at
  build time by the engine's in-process SM83 assembler; a syntax error fails the build with a line
  number. The `28` in `cp a, 28` is the fold modulus (seven kinds times four orientations); it is
  pinned in the tests, so change both together.
- **The rejection rule and the pipeline** are `pickRandomPiece` in `src/vm/piece_random.cpp`. The
  reference the candidate is tested against, the try count, and which fields the accept path writes
  all live there.
- **The register the routine returns** is the binding in `registerPieceRandom` (`.output`). The draw
  core leaves its result in register A.

The routine is baked into the binary (embedded) — no assembly file ships. The build scans the
registration call site to find and assemble it; if you move the registration or rename the assembly
file, keep the path in the `registerRoutine` call a plain string literal at the call site, or the
scan will not find it.

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R PieceRandom
```

The tests construct the VM directly and check the draw domain, the fold relation, determinism, that
the divider feeds and advances the draw, and the `pickRandomPiece` pipeline. They point the asset
root at the project tree so the draw core resolves during the test.
