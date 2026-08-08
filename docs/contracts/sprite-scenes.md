# Sprite scene lists — behavioral contract

Reverse-derived from `kaspermeerts/tetris` (`tetris.asm`), pinned upstream. This document is the
authority the scene-sprite tests are written against. Line anchors (`tetris.asm:NNNN`) cite the
pinned commit.

The port carries the scene **data** — the sprite objects each scripted scene places on screen.
Copying those objects into the object buffer and drawing them are **consumer behavior**: recorded
here as context so the data's shape is justified, but performed by the renderer, not by this unit.

## What a scene sprite is

Each scripted scene — a victory or defeat screen, the ending dance, a launch sequence, a menu
selection — is drawn from a short table of **sprite objects**. An object gives a screen position, the
sprite drawn there, and two OAM attribute toggles. The sprite is named by an index into `SpriteList`,
which is a `SpriteId` (its composed layout is the sprites unit, `docs/contracts/sprites.md`).

Two record shapes exist.

### Shape A — count-prefixed 6-byte objects

Copied by `LoadSprites` (`tetris.asm:3611`): the caller sets a count in `c` and the routine copies
that many 6-byte objects into the `$C200` object buffer, `$10` bytes apart. Eleven tables:

| Label | Objects | Sprites drawn | Consumer |
|---|---|---|---|
| `Data_26CF` (:6286) | 2 | `A_TYPE` ×2 | `:3128` game-type config screen |
| `Data_26DB` (:6291) | 1 | `DIGIT_0` | `:3324` Type-A difficulty screen |
| `Data_26E1` (:6295) | 2 | `DIGIT_0` ×2 | `:3414` Type-B difficulty screen |
| `Data_26ED` (:6299) | 2 | `DIGIT_1` ×2 | `:1043` two-player start-height screen |
| `MarioVictorySprites` (:6303) | 3 | `JUMPING_LARGE_MARIO_1` ×2, `CRYING_SMALL_LUIGI_1` | `:2019` |
| `LuigiVictorySprites` (:6308) | 3 | `JUMPING_LARGE_LUIGI_1` ×2, `CRYING_SMALL_MARIO_1` | `:2023` |
| `MarioDefeatSprites` (:6313) | 2 | `CRYING_LARGE_MARIO_1`, `JUMPING_SMALL_LUIGI_1` | `:2175` |
| `LuigiDefeatSprites` (:6317) | 2 | `CRYING_LARGE_LUIGI_1`, `JUMPING_SMALL_MARIO_1` | `:2179` |
| `DancerSprites` (:6321) | 10 | the ten musicians/dancers | `:4727` |
| `BuranLaunchSprites` (:6333) | 3 | `BURAN`, `ROCKET_SMOKE_1` ×2 | `:2712` |
| `RocketLaunchSprites` (:6338) | 3 | `ROCKET_L`, `ROCKET_SMOKE_1` ×2 | `:2941` |

The victory and defeat screens have Mario and Luigi variants; the two-player code selects between
them by `hSerialRole` (`:2020-2023`, `:2176-2179`).

### Shape B — `$FF`-terminated 7-byte objects

Copied by `CopyUntilFF` (`tetris.asm:6267`), which copies bytes until a `$FF`. Two objects, the
falling-piece templates, copied to `$C200`/`$C210` (`:1252-1256`, `:4178-4182`):

| Label | Bytes | Consumer |
|---|---|---|
| `ActivePieceSprite` (:6280) | 7 data + `$FF` | the active falling piece |
| `PreviewPieceSprite` (:6283) | 7 data + `$FF` | the next-piece preview |

Byte 3 (the sprite index) is `$00` in both — a placeholder the falling-piece code overwrites each
frame with the current piece's rotation sprite. The seventh data byte (base OAM flags) is `$00` in
both; the `$FF` is the copy terminator, not object data.

## The object byte layout

The renderer reads a 7-byte object out of the buffer (`_RenderSprites`, `tetris.asm:6693-6850`):

| Byte | Meaning | Values in the corpus |
|---|---|---|
| 0 | enable / visibility | `$00` draw · `$80` starts hidden (renderer forces Y = `$FF`, `:6829-6833`) |
| 1 | OAM Y base coordinate | full range |
| 2 | OAM X base coordinate | full range |
| 3 | sprite index into `SpriteList` | `$1C`-`$58`, all valid `SpriteId` (`$00` placeholder for shape B) |
| 4 | OAM attribute, OR-merged into the entry (`:6848`) | `$00` or `$80` — only bit 7 (background priority) |
| 5 | OAM attribute + flip control (`:6781`, `:6806`, `:6845`) | `$00` or `$20` — only bit 5 (X-flip) |
| 6 | base OAM flags (shape B only, `:6752`) | `$00` |

Shape A objects are six bytes; the renderer's seventh byte for them is whatever the buffer holds
(the `$10` stride leaves it untouched) and does not come from the table.

### Attribute-bit variance

Across all 35 objects, byte 0 is only ever `$00` or `$80`; byte 4 sets only bit 7; byte 5 sets only
bit 5; the shape-B seventh byte is always `$00`. The port therefore carries three bools — `hidden`
(byte 0 = `$80`), `behindBg` (byte 4 bit 7), `xflip` (byte 5 bit 5) — and asserts the invariant bits
rather than storing them.

`behindBg` is set on every victory and defeat character and on both piece templates (they draw behind
the background layer); it is clear on the dancers, the launch objects, and the menu markers. `xflip`
is set on the mirrored second object of each victory pair and on the second smoke plume of each
launch. The dancers all start `hidden`; the launch smoke plumes start `hidden` while the shuttle or
rocket itself is visible.

## Consumer context (not ported here)

- `LoadSprites` copies six bytes per object with a `$10` stride and marks the object after the last as
  invisible (`:3627`).
- `_RenderSprites` walks the buffer, resolves byte 3 through `SpriteList`, applies the composed
  sprite's own offsets, and expands each object into OAM entries. The `$80`-hidden path forces
  Y = `$FF` (`:6829-6833`); byte-5 bit 5 mirrors the X coordinate as well as setting the OAM flip
  (`:6806-6821`); bytes 4 and 5 are OR-merged into each entry's attribute byte (`:6843-6850`).

The background-priority bit that appears in byte 4 is a data-side use of the same OAM priority bit the
renderer will honor; the runtime attribute plumbing is the renderer's concern.

## Out of scope

The raw four-byte OAM blocks copied straight to the OAM buffer by `Call_725` (`MarioLuigiFaceObjects`
`:1058`, `MarioFaceObjects`/`LuigiFaceObjects` `:1291`/`:1294`, `PushStartObjects` `:2346`) do not use
the `SpriteList` indirection and are not scene sprites; they belong with the miscellaneous object
data. The cursor and music-type coordinate tables are likewise separate. `Data_293E`/`Data_2976`
(`:6510`/`:6519`) are game-over text tilemaps, carried by the tilemaps unit.
