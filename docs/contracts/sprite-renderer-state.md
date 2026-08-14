# Contract — Sprite-renderer state

Reverse-derived behavioral contract for `SpriteRendererState` (`src/state/sprite_renderer_state.h`):
the live sprite-object array the original game keeps at `$C200`, ported as one C++ struct of sixteen
`$10`-byte slots. Every address, use site, and adjudication below is from `tetris/tetris.asm` (upstream
`b95c668`); the array is an anonymous window of `tetris/wram.asm` (it falls inside the
`ds $C300 - $C0DF` gap and is reached only by the renderer's `ld hl, $C200` literals). The line anchors
are the authority the tests check against.

`SpriteRendererState` is an idiomatic surface, not a byte image. It carries the sprite *descriptors* —
the state a scene writes and the renderer reads across frames — and nothing of the renderer's own
transient working memory. The array persists (positions, ids, visibility, dancer animation counters);
every HRAM byte this unit is assigned is alive only inside a single render or lookup call and ports as
**mechanism, not state** (below), the same treatment the audio unit gave the sound driver's RAM.

The boundary between this surface and the renderer mechanism falls exactly on the WRAM/HRAM line: the
`$C200` array is state, and the `$FF86`–`$FF97` / `$FF94` / `$FFB2`–`$FFB5` HRAM bytes the renderer and
the tile lookup use are mechanism.

---

## The slot record

Each slot is `$10` bytes; the game touches nine of them. The renderer copies the first seven bytes of
the active slot into a working record at `$FF86` (`tetris.asm:6719-6726`, a `ld b, 7` loop) and composes
byte +3 against the composed-sprite list `SpriteList`; bytes +$E/+$F are the ending dancers' animation
pair. Bytes +7…+$D
are never reached by any static operand or dynamic walker and are not modelled.

| Offset | Port field | Port type | Role | Anchors |
|---|---|---|---|---|
| +0 | `hidden` | `bool` | visibility status: `$80` = hidden, `$00` = visible | domain closed to `{$00,$80}` (below); read `tetris.asm:6693-6697` |
| +1 | `y` | `uint8_t` | screen Y, OAM convention (+16 hardware offset), raw pixels | "Sprite Y coordinate" `tetris.asm:6776` |
| +2 | `x` | `uint8_t` | screen X, OAM convention (+8 hardware offset), raw pixels | "Active piece X coordinate" `tetris.asm:5966`; read `6799` |
| +3 | `spriteId` | `SpriteId` | composed-sprite id; for a piece it IS the rotation state | index into `SpriteList` `6727-6728`; rotation `5911-5995` (below) |
| +4 | `behindBg` | `bool` | attribute bit 7: draw behind background | OR'd into the final attr `tetris.asm:6848-6850`; set from the scene records' byte 4 |
| +5 | `yflip` / `xflip` | `bool` / `bool` | attribute bit 6 (Y-flip) / bit 5 (X-flip) | honoured `6781` (Y) / `6806` (X); OR'd `6845-6847` |
| +6 | `palette1` | `bool` | attribute bit 4: use object palette 1 (OBP1) | dancers write `$10` at `$C266`/`$C276` `tetris.asm:4730-4734` |
| +$E | `animCounter` | `uint8_t` | dancer animation countdown | init `4735-4748`, tick `4790`+ (below) |
| +$F | `animReload` | `uint8_t` | dancer animation reload value (period) | init `4738-4748`, reload source `4797-4799` |
| +7…+$D | *(omitted)* | — | never accessed | no static operand or walker reaches these offsets |

### The status domain is `{$00, $80}`

Every writer of byte 0 was checked, and each writes exactly `$00` (visible) or `$80` (hidden):
`LoadSprites`' next-slot terminator (`tetris.asm:3627`), the gameplay state handlers `GameState_01`/
`_05`/`_0A`/`_18` (`4578-4580`, `4650-4652`, `4126-4127`, `1219-1220`), the preview-piece toggles
(`1623-1624`, `4197-4201`, `4425-4438`), the pause hide/show pair (`4477-4481`), the multiplayer
type-lock (`888-889`), the smoke hide (`2033-2034`, `2145-2148`), and `LockPieceIntoBackground`
(`6105-6106`). So `hidden` is a `bool`. The renderer has a third branch — any value other than `$00`
or `$80` falls through to `.nextSprite`, skipping the slot and consuming no OAM (`tetris.asm:6693-6698`)
— but no writer in the corpus produces such a value, so it is unreachable and not modelled. If a writer
ever produces a third value, the boolean model is revisited.

### The sprite id is the piece rotation

Byte +3 is the `SpriteId`. For the active piece it doubles as the rotation state:
`RotateAndShiftPiece` manipulates it directly — the four orientations of a tetromino are four
consecutive ids, so a rotation is `dec`/`inc` on the low two bits of `$C203` (`tetris.asm:5925-5949`),
cancelled by restoring the saved id to `$C203` on collision (`5960-5962`). The preview piece's id lives
at `$C213` (slot 1, `5086`/`5160`). `hRocketSpriteIndex` (the game-flow state) is copied into `$C203`
for the celebration rocket (`2945-2946`, `4964-4965`). The piece-rotation ↔ `Piece` link is already
pinned with the composed sprites; this unit only carries the id byte.

### The three attribute bytes

Bytes +4, +5, +6 each carry part of the object's DMG attribute; the renderer OR-composes them into the
final OAM attribute byte (`attr = (byte +6 copy) | (byte +5) | (byte +4)`, `tetris.asm:6843-6850`). The
port unpacks each to the named flag(s) for the bit(s) it carries: +4 → `behindBg` (bit 7), +5 → `yflip`
(bit 6) and `xflip` (bit 5), +6 → `palette1` (bit 4). Only the ending dancers set a bit in the corpus —
`$10` (OBP1) into byte +6 of slots 6 and 7 (`4730-4734`); the scene-sprite records supply the +4/+5 bits
statically. The OR-composition and the per-tile `$FD` x-flip toggle are renderer mechanism, not state.
If any writer sets a bit outside the mapped set, the flag mapping is revisited.

### The dancer animation pair

Byte +$E is a countdown; byte +$F is its reload period. Type-B ending setup seeds ten slots: for each it
writes the same length byte to +$E **and** +$F (a double `ldi`, `tetris.asm:4738-4748`, from the
`.data_1E31` length table). `GameState_23` ticks them: `dec [hl]` on +$E, and at zero it reloads +$E
from +$F and toggles the sprite between its two frames (`4790`-`4806`). The port names both bytes
(upstream has no labels here).

---

## The renderer HRAM is mechanism, not state

The renderer (`_RenderSprites`, `tetris.asm:6687-6856`) and the tile lookup (`_LookupTile`,
`6556-6584`) use a block of HRAM as working memory. No value written to any of these bytes in one render
or lookup call is read by a later call — each is alive only within the call. The port's render bridge
re-implements the walk with locals; this HRAM surface therefore ports as **nothing**, exactly
as the audio unit treated the sound driver's private RAM. Every byte is recorded here with its role and
anchor, and the census test proves each is genuinely still reached (guarding the adjudication against an
upstream repin), but none becomes a field.

| Address(es) | ROM label | Role | Anchors |
|---|---|---|---|
| `$FF8D`/`$FF8E` | `hSpriteRendererOAMHi`/`Lo` | OAM write cursor; seeded per entry point, advances 4 per tile | seed `6221-6250`, advance `6851-6854` |
| `$FF8F` | `hSpriteRendererCount` | remaining-sprites counter for this call | `6705-6708` |
| `$FF90`/`$FF91` | `hSpriteRendererOffsetY`/`OffsetX` | current sprite's base offsets, from the composed-sprite record | `6742-6746` |
| `$FF92`/`$FF93` | `hSpriteRendererObjX`/`ObjY` | per-tile computed screen coords (X-before-Y label quirk) | `hram.asm:30-33`; stored `6798`/`6823` |
| `$FF94` | *(unlabelled)* | per-tile working attribute: copy of byte +6, XOR `$20` on the `$FD` x-flip escape, OR'd into the final attr | copy `6752-6753`, XOR `6759-6761`, OR `6843` |
| `$FF95` | `hSpriteRendererVisible` | visibility latch: set from slot byte 0 (`$80` → Y forced to `$FF`), cleared at every `$FF` terminator | set `6716-6717`, apply `6829-6833`, clear `6711-6714` |
| `$FF96`/`$FF97` | `hSpriteRendererSpriteHi`/`Lo` | saved slot pointer across the compose | `6689-6704` |
| `$FF86`–`$FF8C` | *(unlabelled gap, size 7)* | 7-byte working copy of the slot head; +3 (`$FF89`) reused per tile as the current tile index | copy `6719-6726`, reuse `6775` |
| `$FFB2`–`$FFB5` | *(unlabelled gap)* | `_LookupTile` interface: pixel Y/X in (`$B2`/`$B3`), tilemap address out (`$B4` lo / `$B5` hi) | `6556-6584`; callers `DetectCollision` `6030`, `LockPieceIntoBackground` `6068` |

The `Visible` latch holds a cross-slot invariant: byte 0 sets it, but the clear happens on the *previous*
sprite's `$FF` terminator (`.label_2AA9`, `6711-6714`) before the next slot begins, so a visible slot
that never sets it still reads `$00`. The invariant is renderer mechanism; the port re-establishes it
with a local when the bridge is built.

`Call_2A10` (`tetris.asm:6589`), the inverse of `_LookupTile`, is unused ("Unused?" in the source) and
ports as nothing.

### Escape semantics (renderer, not state)

Composing byte +3 walks the `SpriteList` record, whose tile stream uses three escape bytes: `$FF`
ends the sprite (and clears the `Visible` latch), `$FE` skips a tile advancing the source two bytes, and
`$FD` toggles the per-tile x-flip by XORing `$20` into the working attribute (`tetris.asm:6754-6773`).
The composed-sprite data already resolves this walk statically; it is recorded here as renderer
behavior. The port's bridge replays it against the composed sprites when the bridge is built.

---

## Entry points and OAM base

Three entry points seed the renderer's OAM cursor and count, then call `_RenderSprites` (`6218-6252`):

| Entry point | Count | OAM base | First slot | Meaning |
|---|---|---|---|---|
| `RenderCursors` / `RenderSprites` | 2 (or arg) | OAM entry 0 (`$00`) | `$C200` | menu cursors / general multi-slot render |
| `RenderActivePieceSprite` | 1 | OAM entry 4 (`$10`) | `$C200` | the active piece, "always at the 4th position in OAM" |
| `RenderPreviewPieceSprite` | 1 | OAM entry 8 (`$20`) | `$C210` | the preview piece, "always at the 8th position in OAM" |

The active piece is slot 0 and the preview slot 1 (`kActivePieceSlot` / `kPreviewPieceSlot`); the OAM
base is a renderer input, not slot state.

---

## Slot-role inventory

The sixteen slots have no fixed scene identity beyond slots 0 and 1; each scene lays out the slots it
needs. There is no scene enum (following the scene-sprite tables) — scene usages stay call-site indices,
recorded here:

- **Gameplay:** slot 0 = active piece, slot 1 = preview piece (`tetris.asm:4579-4580`; ids at
  `$C203`/`$C213`).
- **Menus:** slot 0 = music-type cursor, slot 1 = game-type cursor (`3182`, `3259`); the start-height
  cursors reuse slots (`1042-1047`).
- **Victory screens:** three slots, including the sinking-loser smoke at slot 2 (`2025-2160`).
- **Type-B ending dancers:** ten slots (`4726-4763`), animated via the +$E/+$F pair.
- **Celebration rocket:** slot 0 = rocket, slot 1 = launch smoke (`2713-2754`, `2942-2965`;
  `hRocketSpriteIndex` → `$C203` at `2945-2946`).

---

## Boot semantics

Boot is all-zero. The startup clear wipes `$C000`–`$CFFF` (`tetris.asm:319-327`), so a
default-constructed `SpriteRendererState` — every slot all-zero (`hidden == false`, position and id
zero, no attribute bits, no animation) — is the boot state, and `reset()` returns a live instance to it.
Laying out a scene's slots is the job of the systems that own them, not of this struct.

---

## The census, and where fidelity is held

This unit adds no parser work. The work-RAM census already carries the twelve `$C2xx` addresses a
game operand reaches in `tests/fixtures/wram_expected.h`; the high-RAM layout+census fixture carries the
`$FF86` gap (size 7), the ten `hSpriteRenderer*` labels at `$FF8D`–`$FF97`, the `$FF94` gap, and the
`$FF86`–`$FF8C` / `$FF94` / `$FFB2`–`$FFB5` census rows in `tests/fixtures/hram_expected.h`. The twelve
censused `$C2xx` addresses all land on modelled offsets — the observed offset set is `{0,1,2,3,6,$E}`,
every one inside the port's `{0,1,2,3,4,5,6,$E,$F}` slot shape — so the census maps 100% onto the
record; offsets +4/+5/+$F are reached only through pointer walks (`LoadSprites` `3611-3628`, the dancer
loops) and are anchored above rather than censused.

The struct does not mirror the array's byte offsets, so its fidelity is held by the fixtures, not the
memory image: the census offsets must resolve to modelled slot fields, and the HRAM window must still be
present, or an upstream repin surfaces as a test failure.

---

## The surface

- **Parser-emitted** (reused, no delta): `tests/fixtures/wram_expected.h` (the `$C2xx` census rows) and
  `tests/fixtures/hram_expected.h` (the sprite-renderer HRAM layout and census rows). This is the first
  state unit with no parser work — both fixtures already carry every row it consumes.
- **Hand-written port-design:** `src/state/sprite_renderer_state.h` (the `SpriteSlot` /
  `SpriteRendererState` structs, the slot-index constants, and `reset()`), the tests, and this contract.

There is no behavioral code in this unit: the attribute OR-composition, the flip arithmetic, the escape
walk, and `_LookupTile`'s divide-by-8 are renderer work that builds on this struct when the render
bridge is written.

---

## Tested by

`tests/test_sprite_renderer_state.cpp` — the census-offset sweep (every `$C2xx` census address resolves
to a modelled slot offset, with the omitted-offset guard on +7…+$D, and the observed offset set pinned
to `{0,1,2,3,6,$E}` with the `$C200` refCount-24 and `$C266` → slot 6 corner pins); the HRAM window pins
(the `$FF86` and `$FF94` gap rows, the ten `hSpriteRenderer*` labelled rows, and the `$FF86`–`$FF8C` /
`$FF94` / `$FFB2`–`$FFB5` census rows all present); the slot-shape pins (`slots.size()` == the fixture
window arithmetic, defaulted `==`, a default slot == boot state); the reset-to-boot test; and the
`SpriteId`-link pins. The two shipped census guards elsewhere (`tests/test_game_flow_state.cpp`'s
later-surface list and `tests/test_audio_state.cpp`'s `$C200`–`$C300` window) already own these bytes
for this unit; this test refines their per-byte roles.
