# Demo data

Left alone at the title screen, the game plays two attract-mode demos — a Type A game and a Type B game
— by running the normal game loop against recorded input and a fixed piece sequence instead of live
input and the randomizer. This unit ports that recording: the two input timelines and the piece list
they share. It is uncopyrightable algorithmic data — input timings and piece picks, no expressive
content — so it compiles into the binary rather than being extracted from a ROM.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `Action` | `include/kirpich/action.h` | `enum class : uint8_t` — the game's input vocabulary (the piece-control actions) |
| `DemoInputRecord` | `src/data/demo.h` | one recorded step: a `retropp::ActionSet` of held actions + a frame count |
| `kTypeADemoInputs`, `kTypeBDemoInputs` | `src/data/demo.h` | the two demos' timelines (128 and 80 records) |
| `kDemoPieceList` | `src/data/demo.h` | the 48-entry `Piece` sequence both demos replay |

Each recorded step holds a set of game actions for a number of frames, then the next step loads. The
piece list feeds the demos' deterministic piece picks. The behavioral specification — the record
encoding, the button-to-action mapping with source anchors, the shared-list consumption ranges, and the
copy-overrun quirk — is in [`../contracts/demo.md`](../contracts/demo.md).

## Decisions

**Held input resolves to actions, not a hardware byte.** The recordings capture Game Boy joypad state,
but the port surfaces each step as a set of the game's own actions (`kirpich::Action`) carried in the
engine's `retropp::ActionSet`. The engine's action input system reads state keyed by a game-owned action
enum, so the demo replays by feeding these sets into the same input path live play uses — there is no
port-side button type, and the records read as named actions rather than opaque bytes.

**The action vocabulary is the game's, and grows.** `Action` holds the piece controls the gameplay input
handler defines — move left/right, soft drop, rotate clockwise/counter-clockwise. The demos only ever
press move-left/right, soft-drop, and rotate-clockwise; the counter-clockwise action is defined for
completeness. The enum gains enumerators as menu and system input surfaces land.

**The piece list reuses `Piece`.** Each list byte is a piece spec in the same `kind × 4 + rotation`
encoding the piece logic uses, so `kDemoPieceList` is a `std::array<Piece, 48>` — every entry is a
spawn-orientation piece (rotation 0).

**The data commits into the binary.** Unlike the graphics and audio payloads, the demo blobs are
algorithmic data, so they are ported as `constexpr` data in `src/data/demo.h` rather than extracted into
`assets/` at runtime.

## Keeping it honest

`src/data/demo.h` and the fixture are generated from the disassembly by
`tools/asm_parser/parse_demo.py`, which reads the three `.bin` files directly, anchors their `INCBIN`
directives in `tetris.asm`, and stops with a source citation if a blob is the wrong size, a piece byte is
not a spawn-orientation spec, or a held byte presses a button with no mapped action. The fixture holds
each blob's real bytes independently of the composed records, and `tests/test_demo.cpp` bridges the two —
resolving every raw held byte through the button-to-action mapping and comparing it to the record — so a
defect in the header or a wrong mapping fails the sweep. See [`../engine/demo.md`](../engine/demo.md) for
how to use and regenerate it.

## Not here yet

Playing the demos — feeding the action sets into the input path, deriving the pressed edge from the
held-set transitions, selecting between the two demos, and ending a demo by piece count — is the
demo-replay work, and it builds on this data. Binding physical controls (keyboard, gamepad) to the
`Action` vocabulary is the input system's work. This unit provides the recorded actions and piece
sequence those build on, and the fixture to check them against.
