# Demo data

The two attract-mode demo recordings and the piece sequence they share. When the title screen is left
alone the game plays a Type A demo and a Type B demo by running the normal game loop against recorded
input and a fixed piece list. This surface is that recording: two timelines of held game actions, and
the 48-entry piece list both demos draw from.

Each recorded step is a set of game actions (`kirpich::Action`) and a frame count — the game holds that
action set for that many frames, then advances to the next step. The recordings capture Game Boy joypad
state; the port resolves each button bit to the action the gameplay input handler binds it to, so the
records carry actions the demo-replay system feeds straight into the engine input path, not hardware
bytes.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `include/kirpich/action.h` | The `Action` enum — the game's input vocabulary | Hand-written; extend as input surfaces land |
| `src/data/demo.h` | `DemoInputRecord`, the `heldActions` builder, the two record arrays, `kDemoPieceList`, and the count constants | **Generated — do not hand-edit.** |
| `tests/fixtures/demo_expected.h` | The three blobs as flat raw bytes | **Generated — do not hand-edit.** |

Everything is in `namespace kirpich` (the fixture in `namespace kirpich::fixtures`), included as
`"data/demo.h"` and `<kirpich/action.h>` (the `src/` tree and `include/` are on the library's include
path). `demo.h` includes `<retropp/input.h>` for `retropp::ActionSet`; the surface is header-only, with
no `.cpp` and no accessor — it is the data the demo-replay code reads.

## Using it

```cpp
#include "data/demo.h"

#include <retropp/input.h>
#include <kirpich/action.h>

using kirpich::Action;
using kirpich::kTypeADemoInputs;
using kirpich::kDemoPieceList;

const kirpich::DemoInputRecord& step = kTypeADemoInputs[0];
bool dropping = step.held.test(retropp::actionId(Action::SoftDrop));  // is soft drop held this step?
std::uint8_t frames = step.frames;                                   // frames the state holds

kirpich::Piece piece = kDemoPieceList[0];   // the demo's first piece; piece.kind() / piece.rotation()
```

`DemoInputRecord` is `{ retropp::ActionSet held; std::uint8_t frames; }` with a defaulted `==`. `held` is
the engine's action set — read an action with `held.test(retropp::actionId(Action::X))`. `frames` is how
many frames the set persists before `kTypeADemoInputs[i + 1]` loads. The streams do not carry an end
marker; a demo ends by piece count, so the trailing records (all empty, zero frames) are never reached.

`kTypeADemoInputs` has `kTypeADemoInputCount` (128) records, `kTypeBDemoInputs` has `kTypeBDemoInputCount`
(80), and `kDemoPieceList` has `kDemoPieceCount` (48) `Piece`s. Each piece is a spawn-orientation spec
(`rotation() == 0`); the Type A demo reads indices 0–15 and the Type B demo 17–29.

`heldActions(std::initializer_list<Action>)` is the `constexpr` builder the records use — it is how the
generated data names each held set (`heldActions({Action::MoveLeft})`), and it is available for any code
that needs to build an action set from named actions.

The actions a held set can contain are the piece controls: `MoveLeft` / `MoveRight` (LEFT / RIGHT),
`SoftDrop` (DOWN), and `RotateClockwise` (A). `RotateCounterClockwise` (B) is in the `Action` enum but
never appears in either recording. Binding physical controls to these actions is the input system's job,
not this data's.

## Regenerating the data

`demo.h` and the fixture are produced from the disassembly by the parser. Regenerate after repinning the
upstream source:

```sh
python3 tools/asm_parser/parse_demo.py \
  --source-root ../tetris \
  --all \
  --demo-out    src/data/demo.h \
  --fixture-out tests/fixtures/demo_expected.h
```

The parser reads the three `.bin` files directly and anchors the three `INCBIN` directives in
`tetris.asm`. It stops with a source citation if a blob is the wrong size, a stream has an odd byte
count, a piece byte is not a spawn-orientation spec, or a held byte presses a button with no mapped
action. It does not emit `action.h` — the `Action` vocabulary is hand-written. Python 3 (standard library
only); it is a development tool and is never needed to build or test Kirpich.

## Changing it

The recordings, the piece list, and the button-to-action mapping are fixed by the original and are not
tuning knobs. The generated files are overwritten on the next parser run, so never hand-edit them. To add
an action the vocabulary lacks (a menu or system action), add an enumerator to `Action` in `action.h`.

## Testing

`tests/test_demo.cpp` sweeps both record arrays in full, resolving each raw fixture byte to the action
set the record must carry (so a wrong button-to-action mapping fails), sweeps the piece list with the
spawn-orientation domain checked across the corpus, confirms the recordings press only the five mapped
buttons and never rotate counter-clockwise, and pins the corners and the consumed piece-index ranges. The
parser has its own tests (`tools/asm_parser/test_parse_demo.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_demo`). The behavioral specification, with source line
anchors, is in [`../contracts/demo.md`](../contracts/demo.md).
```
