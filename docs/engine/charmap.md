# Charmap

The character map turns text into tile indices. It is the first data table in Kirpich with a `.cpp`
behind it: a 47-row table plus three lookups.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `include/kirpich/charmap.h` | `CharmapEntry` (the row type) | Hand-written. |
| `src/data/charmap.h` | The three accessor declarations | Hand-written. |
| `src/data/charmap.cpp` | The table + accessor bodies | Bodies hand-written; the table rows are **generated**. |
| `src/data/generated/charmap_data.inc` | The 47 rows | **Generated — do not hand-edit.** |
| `tests/fixtures/charmap_expected.h` | The same 47 rows, for the test sweep | **Generated — do not hand-edit.** |

`CharmapEntry` is in `namespace kirpich`; include it as `<kirpich/charmap.h>`. The accessors are in
`src/data/charmap.h`, included as `"data/charmap.h"` (the `src/` tree is on the library's include
path).

## Using it

```cpp
#include "data/charmap.h"

kirpich::getCharmapTile("×");              // -> std::optional<std::uint8_t>{0x26}
kirpich::getCharmapTile("A");              // -> std::nullopt (uppercase is not mapped)

kirpich::encodeCharmapText("pause");       // -> {0x19, 0x0A, 0x1E, 0x1C, 0x0E}
kirpich::encodeCharmapText("pazhitnov.”");  // greedy longest match: ".”" encodes to one tile ($9D)
kirpich::encodeCharmapText("!");           // -> std::nullopt (unmapped char fails the whole string)
kirpich::encodeCharmapText("");            // -> {} (empty succeeds)

for (const auto& e : kirpich::getCharmap()) { /* the whole table */ }
```

- `getCharmapTile(sequence)` matches a whole sequence exactly — no prefix logic. Use it for single
  characters.
- `encodeCharmapText(utf8)` encodes a whole string: at each position the longest matching sequence
  wins, and any character with no entry makes the whole call return `std::nullopt` (no partial
  output). Pass UTF-8 bytes; the multi-byte characters (`×`, `♥`, `…`, the `.”` ligature, …) match on
  their exact bytes.

## Regenerating the table

`charmap_data.inc` and the test fixture are produced from the disassembly by the parser. Regenerate
after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_charmap.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/charmap_data.inc \
  --fixture-out tests/fixtures/charmap_expected.h
```

The parser checks the source's structure as it reads — the entry count, that no sequence or tile
repeats, that the digits and letters land on their expected runs, and that the one ligature is the
sole multi-character sequence — and stops with a citation if anything has moved, rather than emitting
a wrong file. Non-ASCII characters are written as `\xHH` byte escapes so the generated files are pure
ASCII and compile identically on every toolchain. Python 3 (standard library only); it is a
development tool and is never needed to build or test Kirpich.

## Changing it

To change what a character maps to, change the source and regenerate — never hand-edit the generated
files, since the next run overwrites them. To change the lookup or encoding *behavior*, edit
`src/data/charmap.cpp`; the table rows stay generated.

The exact meaning of every entry, with its line in the original, is in
[`../contracts/charmap.md`](../contracts/charmap.md).

## Testing

`tests/test_charmap.cpp` sweeps the whole table against the generated fixture, checks digit- and
letter-identity, exercises exact lookup and the longest-match encoder (including two real upstream
strings), and covers the unmapped-input and empty-input cases. The parser has its own tests
(`tools/asm_parser/test_parse_charmap.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_charmap`).
