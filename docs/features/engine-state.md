# Global game state

The mutable global state a running game reads and writes frame to frame — the score and its line-clear
bookkeeping, the sprite staging buffer the renderer flushes each frame, and the piece ring the randomizer
fills — ported as one hand-written C++ struct. It is the first of the state types and the one nearly every
gameplay system touches.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `EngineState` | `src/state/engine_state.h` | the whole state block: score, line-clears, stats, soft-drop points, scoreboard flags, the OAM staging buffer, and the piece ring |
| `OamEntry` | `src/state/engine_state.h` | one live sprite-staging entry: `{ y, x, tile }` plus the four unpacked DMG object-attribute flags |
| `LineClearStats` | `src/state/engine_state.h` | the four line-clear tallies: `{ singles, doubles, triples, tetrises }` |

The behavioral specification — the field-by-field mapping to the original's RAM addresses, the collapsed
and omitted bytes, and the use-site anchors for the three unlabelled flags — is in
[`../contracts/engine-state.md`](../contracts/engine-state.md).

## Decisions

**One header-only struct.** The state is a plain struct with a `reset()` that returns it to the all-zero
boot state; there is no `.cpp`. Filling a new game's state (seeding the piece ring and so on) is the job of
the systems that own those fields, not of the struct.

**The score is decimal.** The original keeps the score as three packed-decimal bytes and a separate
packed-decimal scratch copy for the soft-drop tally. The port stores a single decimal `uint32_t` and
converts to packed decimal only at print time; the scratch copy is not stored. Soft-drop points are a
plain `uint16_t`.

**Line-clears are row indices.** The original stores up to four field-row *addresses* ending in a zero
word. The port stores the equivalent row indices in a four-entry bounded vector and uses its length in
place of the terminator — the port has no address space to mirror, and the indices are what the consumers
want.

**Sprite staging carries the full attribute set.** An `OamEntry` unpacks the DMG object-attribute byte
into four named flags (priority, Y-flip, X-flip, palette). Live staging can set any of them, so all four
are represented — unlike the static object tables in the data layer, which only ever set X-flip and keep a
narrower shape.

**Three flags the original never named.** Between the scoreboard state and the preview-hide flag sit three
single-byte values the original reads and writes without labelling. They are given role names here
(`scoreboardTallyPhase`, `blockSoftDropAfterLock`, `scoreRedrawRequested`) and anchored to their use sites
in the contract.

## Keeping it honest

The struct does not mirror the original's byte offsets, so its fidelity is checked a different way. A
parser (`tools/asm_parser/parse_wram.py`) walks the original's whole RAM map and emits a layout fixture —
every label with its address and size — deriving each address from the section origins and the space
reserved before it, and stopping with a source citation if the map's own address arithmetic does not line
up. `tests/test_engine_state.cpp` proves the layout tiles each RAM section with no overlap or hole and
pins the struct's widths (the 40-entry OAM buffer, the 256-entry piece ring, the four-entry line-clears
cap, the stat counts, the soft-drop width) against that fixture, then checks `reset()` and the value types
behave. See [`../engine/engine-state.md`](../engine/engine-state.md) for how to use and regenerate it.

## Not here yet

The state block is data; the code that mutates it — the randomizer that fills the piece ring, the
line-clear logic that records cleared rows, the scoring that drives the count-up animation, and the
renderer that flushes the staging buffer — is gameplay and rendering work that builds on this struct. The
board's own shadow state (the playing-field cells and the staging row that feed the renderer) is a separate
state type. This unit provides the struct, its lifecycle, and the layout fixture to check it against.
