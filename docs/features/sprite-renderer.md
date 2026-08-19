# Sprite renderer

**Date:** 2026-08-18
**Status:** Complete

## Concept

Everything on screen that is not the background: the piece you are moving, the next one waiting, every
menu cursor, and the ten performers of the ending's dance. The game keeps sixteen sprite descriptors —
a position, a composed sprite, a few attribute flags — and one flat buffer of forty hardware object
entries. This unit is the routine that turns the first into the second, restored at the call sites the
handlers make it from, and the bridge that hands the finished buffer to the renderer.

Before this, the simulation had been filling those descriptors since the piece system landed and
nothing ever read them. The background drew; the pieces did not. A round ran, but a player could not
see what they were doing.

## Design decisions

### The renderer is a simulation routine; only the bridge knows the renderer

The walk over descriptors is the game's own routine and writes the game's own buffer, so it lives with
the other systems and knows nothing about the engine. The bridge reads the finished buffer and knows
nothing about descriptors. Splitting them that way keeps the buffer the single meeting point, which is
also what lets the entries the game fills in directly — the title screen's cursor — reach the screen
without a second path.

### The calls go where the game makes them, not once per frame

A once-per-frame redraw was rejected. The buffer persists between calls, the four entry points write
different windows of it, and handlers interleave direct writes with drawn ones — clearing the buffer,
then drawing cursors over the front of it. Composing it once at the end of a frame would produce a
different buffer than the game produces, in ways that depend on which handler ran.

It also means the redraw's *absence* is ported: the menu screens redraw on the paths that stay on the
screen and not on the paths that leave, and the drop redraws on two of its three exits.

### One position law, shared with collision

The piece system already reproduced the unflipped half of the placement arithmetic, because in the
original the renderer is also what collision reads — the piece is drawn into the buffer and the buffer
is read back. Rather than keep two copies, the law moved to the renderer and the piece system consumes
it. The two flip branches, which collision never needed, came with it.

The alternative — leaving the piece system's copy alone and writing a second one — was rejected because
the two would then be free to disagree, and the thing they compute is where a piece is.

### Objects get their own palettes, and the see-through entry is the last one

An object's lowest hardware colour is see-through rather than a shade; the background has no such
colour. Reusing the background's palette draws every object on a solid card, so the object palettes are
separate.

Which entry is see-through is the part worth writing down: the decode inverts, so a sample's index
counts down from the darkest shade while the hardware colour it came from counts up. The see-through
colour is the hardware's lowest and therefore the port's **highest** index — the last entry of the
ramp, not the first.

Three palettes ship. The game keeps two object palettes and uses both — the ending draws two of its ten
performers through the second — and the font needs neither variant, because its tiles only ever carry
the darkest colour or the see-through one and both palettes agree on those two.

### An object is named for what it is, and the name changes every tick

The engine matches an object to its previous frame by name and eases it between the two positions.
Both halves of the naming rule exist because of that.

Naming an object after the buffer entry it landed in was tried and is wrong: entries are a shared
resource, so a wider sprite pushes everything after it along and a screen change refills the buffer
entirely, and two unrelated objects sharing a name read as one that travelled across the screen. The
name is instead which descriptor drew it, which sprite that descriptor was drawing, and which part of
that sprite it is — which the renderer records as it writes.

The second half is a counter that advances every tick. Nothing in this game moves continuously: every
object sits on the 8-pixel grid and moves a whole step at a time, so a move should arrive rather than
glide. A name the renderer has not just seen cannot be matched against anything, which is what makes it
arrive.

A single alternating bit was tried for that and is not enough. The renderer forgets a name when a
submission arrives without it, so a stale position survives exactly one submission — and one submission
can span several ticks, which the run loop bounds at fourteen. An alternating tag comes back around on
any two-tick frame and hands the renderer the name it saw last time, with real movement in between; the
object glides. A counter cannot return that fast.

Turning the engine's interpolation off instead was rejected: that switch is also what holds the frame
rate steady on a display refreshing faster than the simulation ticks. Easing is unwanted here; the
pacing that comes with it is not.

### Entries that draw nothing are not submitted

An entry whose position puts it off the screen draws nothing, and is left out entirely rather than
submitted and clipped. Submitting it would give it a position for the next frame to be eased from, so
an object that comes back would slide in from wherever it had been parked instead of appearing.

## Implementation details

- `src/systems/sprite_renderer.{h,cpp}` — the walk and the four entry points, plus the shared position
  law.
- `src/systems/oam_source.h` — what the renderer records about each entry it writes.
- `src/render/sprites.{h,cpp}` — the bridge: which entries are submitted, where they land, what they
  are called, and the layer they are handed over in.
- `src/render/tile_atlas.{h,cpp}` — the three object palettes join the uploaded set.
- The calls are restored in `menu_screens.cpp`, `piece.cpp`, `gameplay.cpp` and `type_b_ending.cpp`.

Two things a round needs that were missing and surfaced by playing it: the round init seeds both piece
descriptors from their stored templates, which is where the preview gets the position that puts it in
its box; and the host runs the frame's last beat, the one the original runs in its vertical-blank
handler, without which a round stops after its first lock.

The exact laws, with source line anchors, are in
[`../contracts/sprite-renderer.md`](../contracts/sprite-renderer.md).

## Open questions / future work

Three hardware behaviours are carried but not reproduced, and are listed in the contract: object-over-
background priority, the per-scanline object limit, and left-to-right priority.

The line-clear flash and the game-over curtain's sweep are not visible. Both live in the difference
between the game's own copy of the screen and the copy the hardware displays — the flash alternates the
displayed rows while the game's copy keeps the blocks, and the curtain is carried into the display a
row at a time. The port keeps one copy, so neither effect has anywhere to happen. The paused screen is
the same shape of gap: it is a second displayed screen the port does not model.
