# Contract — Sprite renderer and sprite bridge

Reverse-derived behavioral contract for compiling Kirpich's sprite descriptors into the object buffer,
and for turning that buffer into drawn objects. Every value here is transcribed from the
`kaspermeerts/tetris` disassembly (upstream `b95c668`); the line anchors are the authority the tests
check against.

The composition a descriptor points at is already resolved — one composed sprite per identity, parts
carrying their own offsets and flips — and is specified in [`sprites.md`](sprites.md). This document
covers what the renderer does with those parts, and what the bridge does with the result.

---

## 1. The four entry points

`_RenderSprites` (`tetris.asm:6687`–`6856`) draws a run of descriptors into a run of buffer entries.
Four wrappers fix the two choices (`:6218`–`6252`):

| Routine | Anchor | Descriptors | First buffer entry | Drawn by |
|---|---|---|---|---|
| `RenderSprites` | `:6220` | `count`, from the first | 0 | the ending's dance (10) |
| `RenderCursors` | `:6218` | 2, from the first | 0 | every menu screen |
| `RenderActivePieceSprite` | `:6231` | 1, the first | 4 (`$10`) | the piece system, the round init, pause, game over |
| `RenderPreviewPieceSprite` | `:6243` | 1, the second | 8 (`$20`) | the piece pipeline, pause, game over |

The first two descriptors are the gameplay-stable pair — the active piece and the preview — and every
other screen reuses them for its own objects.

## 2. The walk

Per descriptor, then per part of that descriptor's composed sprite.

### The status byte has three cases

`:6693`–`:6697`. `$00` draws the descriptor; `$80` draws it **hidden**; any other value **skips the
descriptor entirely**, touching no entry and consuming no buffer position (`:6698`, the fall-through).
The port models the status as a `bool`, so the third case cannot arise from shipped state — the walk
is written so that the first two are the shape of the code.

### The buffer cursor runs on

`:6825`–`:6854`. The cursor advances one entry per drawn part and is **never reset between
descriptors**, so two descriptors drawn from entry 0 occupy as many consecutive entries as their parts
total. This is why the cursor pair fills the front of the buffer and why a wider sprite pushes
everything after it along.

Entries the walk does not reach keep what they held: the buffer is persistent, and a caller that wants
it blank clears it first (`ClearObjects`, `:3630`–`:3638`).

The ten performers of the dance are the widest scene the game draws, and at four parts each they fill
the forty entries **exactly**, to the last one.

### A hidden descriptor still draws

`:6829`–`:6833`. Hidden substitutes `$FF` for the entry's y and nothing else: the real x, tile and
attributes are written as usual. The piece system depends on this — a hidden piece still occupies the
columns it really covers, which is what lets collision and locking read it (see
[`piece-system.md`](piece-system.md)).

### The position law

`:6774`–`:6823`. Three terms compose per axis: the descriptor's coordinate, the sprite's render
offset, and the part's offset within the sprite.

- **Unflipped** (`:6783`–`:6786`): `add` the descriptor's coordinate to the render offset, then `adc`
  the part offset — so the carry out of the first add lands in the second.
- **Flipped** (`:6788`–`:6796` for y, `:6813`–`:6821` for x): `sub` / `sbc` on the same borrow chain,
  then a further 8 pixels off. A flipped tile is placed by its far edge.

Everything is 8-bit and wraps. The leaked carry is not incidental: the piece sprites' render offsets
are negative, so the first step overflows on every part of every piece, and the carry is part of where
a piece appears. The flip flags are the descriptor's own (`:6781` tests bit 6 for y, `:6806` bit 5 for
x), not the part's.

### The attribute merge is an OR

`:6841`–`:6850`. The final attribute is the descriptor's three attribute sources merged with the
part's own flip, and the merge is an **OR**. So a part's flip can set the horizontal bit but can never
clear one the descriptor already carries: a flipped part inside a flipped descriptor stays flipped.
The three sources map onto the port's named flags — background priority, vertical flip, horizontal
flip, and which object palette (see [`sprite-renderer-state.md`](sprite-renderer-state.md)).

## 3. Where the calls are made

The renderer is called from the handlers, not once per frame, because the buffer persists between
calls, the entry points write different windows of it, and handlers interleave direct writes with
drawn ones. Twenty-six sites across the four handler families the port draws:

| Family | Sites | Anchors |
|---|---|---|
| Menu screens | 8 | `:3143`, `:3210`, `:3296`, `:3331`, `:3387`, `:3425`, `:3488`, `:3551` |
| Piece system | 9 | `:5161`, `:5208`, `:5226`, `:5233`, `:5954`, `:5963`, `:5988`, `:6000`, `:6024` |
| Gameplay session | 6 | `:4206`, `:4433`, `:4482`, `:4483`, `:4581`, `:4582` |
| Type B ending | 3 | `:4653`, `:4654`, `:4781` (and `:4817`) |

Two patterns are worth naming because a redraw's absence is as deliberate as its presence:

- **The menu screens redraw on the paths that stay on the screen and not on the paths that leave.**
  Every d-pad path — including the end-stops that change nothing — reaches the shared exit that
  redraws; the transitions that write a new game state do not (`:3296` vs `:3300`–`:3313`).
- **The drop redraws on two of its three exits.** The shared exit redraws (`:5208`), and so do the
  step and its revert (`:5226`, `:5233`); the two gates that return because the stack is still falling
  do not (`:5214`, `:5217`).

In the original the redraw is also what collision reads — it renders into the buffer and reads that
buffer back (`:5954`–`:5955`). The port computes the piece's cells from the descriptor directly, so
here the calls are display only and cannot change the simulation.

## 4. What the round init seeds

`GameState_0A` copies both piece descriptors from stored templates before drawing anything
(`:4176`–`:4181`, via `CopyUntilFF` `:6267`–`:6276`). That copy is where the two pieces get their
screen positions and attributes — in particular the preview's, which is the only thing that puts it
in its box. The templates are the two piece-template records in
[`sprite-scenes.md`](sprite-scenes.md); both set background priority.

Startup seeds two more values the screens that follow read as drawing inputs (`:371`–`:376`): the game
type, which doubles as a cursor coordinate, and the music type, which doubles as a sprite identity.

## 5. The bridge

The buffer is the port's model of the hardware's object list, so the objects on screen are a function
of it and of which tile art is live.

**Placement.** An entry's stored coordinates are offset from the screen's: a stored y of 16 is the top
row, a stored x of 8 the first column. An entry that puts no pixel on the screen is not submitted at
all — an untouched entry sits above and left of the first pixel, a hidden one below the last.

**Art and palette.** Objects index the same tile block the loaders write, so a tile index means the
same picture it means for the background and resolves through the same relation (see
[`tile-graphics.md`](tile-graphics.md)). The palette does not: objects have their own, and the entry's
attribute chooses between the two.

**Names.** Each object is named for what it is — which descriptor, which composed sprite, which part —
never for the entry it landed in, since entries shift whenever a descriptor's art changes width. The
name also carries a per-tick counter, which is what makes a moved object arrive rather than glide:
every object here steps a whole tile at a time, once a tick.

## 6. The object palettes

The game writes two object palettes once at startup and never changes them (`:296`–`:300`):
`rOBP0 = %11100100`, the plain ramp, and `rOBP1 = %11000100`, which draws the second-darkest colour
lightest instead. The ending selects the second for two of its ten performers.

Two properties decide how they are built:

1. **The lowest hardware colour is see-through**, not a shade. That is what makes an object a shape
   rather than a rectangle, and it is why an object cannot share the background's palette.
2. **The decode inverts** (see [`tile-graphics.md`](tile-graphics.md)), so a sample's index counts down
   from the darkest shade while the hardware colour it came from counts up. The see-through colour is
   therefore the **last** entry of each ramp, not the first.

The font's expansion writes each source byte into both halves of the tile row (`LoadFontTiles`,
`:6383`–`:6387`), so a font tile only ever carries the darkest colour or the see-through one — and both
object palettes agree on those two, so one font palette serves whichever is selected.

## 7. Recorded differences

Three hardware behaviours are not reproduced. None is describable as complete:

- **Object-over-background priority.** The attribute is carried through to the entry and stops there.
  Both piece templates set it, so it is live in every round; in ordinary play it is invisible, because
  collision keeps a falling piece off every non-blank cell.
- **The per-scanline object limit.** The hardware drew at most ten objects on a row and dropped the
  rest, which made crowded rows flicker. Every object is drawn here.
- **Left-to-right priority.** On the hardware a further-left object won outright, whatever its entry.
  Here a lower entry simply draws on top, which is the hardware's tie-break but not its main rule.

## 8. Tested by

`tests/test_sprite_renderer.cpp` covers the walk: the four entry points' descriptor ranges and buffer
windows including the cursor running on across descriptors, the status cases, the position law swept
over all 94 sprites in both flip states against the law re-derived from the routine, the OR merge
including a flipped part inside a flipped descriptor, and the recorded identity.

`tests/test_sprite_bridge.cpp` covers the bridge: which entries are submitted and the boundary either
way, the art and palette selection per regime and attribute, the palette relation above, the naming
rules, and the layer's shape.

`tests/test_piece_system.cpp` covers the position law from its other consumer — the two share one
implementation, so a change to the law fails both.
