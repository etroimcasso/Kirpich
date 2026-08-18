# Type B ending — behavioral contract

What happens after a Type B round is won: the scoreboard the player is shown, and — for a round
started on the hardest level — the dance that runs first. Derived from `tetris.asm`; every behaviour
below carries its source line.

Scope: the three states a won round passes through. The results count-up that runs on the scoreboard
screen is specified with the scoring work; the Buran launch a height-5 round enters afterwards is
specified with the ending scenes.

---

## 1. The states

| State | Handler | Source | Role |
|---|---|---|---|
| `$05` | `typeBVictoryJingle` | `:4617–4660` | Load the scoreboard, print the round's scores, hand off to the count-up |
| `$22` | `initBonusEnding` | `:4718–4773` | Lay out the dance: the backdrop, the ten performers, the jingle |
| `$23` | `dancers` | `:4784–4841` | Animate the performers until the jingle ends |

### 1.1 How a round reaches them

The line-clear terminal writes one of two states when the last line of a Type B round is cleared
(`:5778–5796`): `$22` when the round was played on level 9, `$05` otherwise. Both paths end at `$05`
— `$23` exits to it unless the round was started at garbage height 5, in which case the Buran launch
(`$26`) runs instead and the scoreboard is never shown. `$05` itself exits to `$0B`, which drives the
results count-up.

```
level 9 ──► $22 ──► $23 ──┬── height 5 ──► $26  (Buran launch)
                          └── otherwise ──► $05 ──► $0B  (count-up)
level 0-8 ────────────────────────────────► $05 ──► $0B
```

---

## 2. Loading a screen into the playing field

`$05` and `$22` both draw their screen by copying a field-shaped tilemap into the board through
`LoadPlayingFieldTilemap` (`:6434–6457`), starting at `$C802` — the field's own top-left cell.

The routine walks rows of ten cells at the board's `$20`-cell stride, and it stops on an `$FF`
sentinel rather than on a count (`:6440–6441`). **On reaching the sentinel it sets the wipe step to 2**
(`:6453–6456`), starting the row-by-row wipe animation over the screen it has just drawn.

Two things about that are contract, not incidental:

- **The wipe is armed after the copy, not before.** The counter is written only on the sentinel, so
  every cell of the screen is in place before the animation starts. The field fill the gameplay
  session uses (`:5039–5043`) arms *first* and is the mirror image of this one; the two are not
  interchangeable and their orders are both observable.
- **The sentinel is a property of the stored bytes, not of the screen.** The stored tilemaps are
  10×18 grids followed by a terminator byte; a copy walks all 180 cells and then stops. Since the
  port's tilemaps are already 10×18 grids with no terminator, it walks the grid and arms the wipe
  after the last row — there is no sentinel to test for, and none is invented.

The routine's own two loop labels are swapped with respect to what they iterate (`.columnLoop` steps
rows, `.rowLoop` steps the ten cells within a row). This changes nothing; it is noted so a reader
comparing the two does not conclude the port has the axes crossed.

---

## 3. `$05` — the scoreboard

Runs once the frame timer reaches zero (`:4618–4620`); until then the state does nothing.

1. **Draw the scoreboard** (`:4621–4623`) — the scoreboard tilemap, through §2. The stored screen
   already reads `0 × 40`, `0 × 100`, `0 × 300`, `0 × 1200`: the *level 0* values.

2. **Fork on the level** (`:4624–4626`). A round played on level 0 skips step 3 and step 4 entirely —
   the drawn screen is already correct for it. This is not an optimisation to be folded away: the
   skip is why the level-0 screen shows the base values and every other level overwrites them.

3. **Print the four score rows** (`:4627–4638`) — for each line-clear kind, the points that kind is
   worth at this round's level, written as decimal digits into the field:

   | Kind | Field row | Field column |
   |---|---|---|
   | single | 1 | 5 |
   | double | 4 | 5 |
   | triple | 7 | 5 |
   | tetris | 10 | 5 |

   The value printed is the kind's base award multiplied by one more than the Type B level
   (`:4671–4678`), left-aligned with leading zeros suppressed. The printing routine and its arithmetic
   are specified with the scoring work; this state supplies the kind and the destination.

4. **Zero the score** (`:4639–4645`). The original prints those four rows by borrowing the score bytes
   as a scratchpad (`:4664–4666`), and this clears the scratch afterwards. The port's printing does
   not borrow the score, so the write here stands on its own — it is still made, because the state the
   original leaves behind is a zeroed score, and the count-up that follows starts from zero. It sits
   inside the fork: a level-0 round never reaches it, and never dirtied the score to begin with.

5. **Finish** (`:4647–4659`), on both arms of the fork:

   - frame timer to 128 — a little over two seconds;
   - hide both piece sprites — the active piece and the preview (`:4650–4652`);
   - re-initialise the sound driver (`:4655`);
   - line count to 25 (`:4656–4657`);
   - state to `$0B`.

   **The line count is 25, in the ordinary decimal sense.** The original stores it as packed decimal
   and writes the byte `$25`, which is the number twenty-five — its own comment says so. The port
   carries a decimal count, so it writes 25. Reading that byte as hexadecimal would put 37 on the
   screen, and nothing else would go wrong, which is what makes the mistake worth naming here.

---

## 4. `$22` — laying out the dance

Runs once the frame timer reaches zero (`:4719–4721`).

1. **Draw the dance backdrop** (`:4722–4724`) — the dancers tilemap, through §2.

2. **Clear the object buffer** (`:4725`).

3. **Load ten performers** (`:4726–4729`) into sprite slots 0–9 from the stored dancer scene list.
   Every one of them starts hidden; step 6 decides how many become visible.

4. **Second palette on slots 6 and 7** (`:4730–4734`) — the jumping cossack and the dancer beside him.

5. **Seed each performer's animation** (`:4735–4748`). A ten-entry table (`:4776–4777`) gives one
   period per slot:

   | Slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
   |---|---|---|---|---|---|---|---|---|---|---|
   | Period | `$1C` | `$0F` | `$1E` | `$32` | `$20` | `$18` | `$26` | `$1D` | `$28` | `$2B` |

   The value is written **twice** per slot (`:4740–4741`): once as the live countdown and once as the
   reload the countdown restarts from. They are equal at the start and stay equal for the rest of the
   scene, so each performer moves at its own fixed rate and the ten of them never fall into step.

6. **Decide how many are visible** (`:4749–4763`): one more dancer than the starting garbage height —
   except at height 5, which gets all ten rather than six. The original's own comment states the rule
   and the exception. Slots `0 … n-1` are revealed.

   | Start height | 0 | 1 | 2 | 3 | 4 | 5 |
   |---|---|---|---|---|---|---|
   | Visible | 1 | 2 | 3 | 4 | 5 | **10** |

7. **Cue the jingle** (`:4764–4766`) — the Type B jingle at the round's starting height: height 0 gets
   the first, height 5 the sixth. The original's comment on the arithmetic: "Jingles 0A to 0F sound
   better and better."

8. **Finish** (`:4767–4772`): line count to 25 (§3, same value and same reason — the original marks
   this one "TODO why?"); frame timer to 27, which its comment calls "almost half a second. Super
   random number"; state to `$23`.

---

## 5. `$23` — the dance

Two gates run first (`:4785–4789`):

- **at exactly 20 on the frame timer**, the state redraws the performers and does nothing else
  (`:4786–4787`, jumping into `:4779–4782`). Redrawing is display work, so in the port this frame is a
  plain return. It is a real branch of the original and is carried as one, rather than being merged
  into the gate below, because the two are not the same: this one runs *instead of* the animation on
  that frame, and the animation would otherwise be blocked anyway by the next gate.
- **otherwise, a non-zero frame timer** returns (`:4788–4789`).

Then each of the ten slots is stepped (`:4790–4815`):

- Count the slot's animation counter down by one. Unless it reaches zero, the slot is left alone.
- On zero: reload the counter from the slot's reload value (`:4797–4799`).
- **Flip the sprite to its other frame** by toggling the low bit of its id (`:4800–4806`). Every
  performer's two frames are a consecutive pair, so the toggle moves between them in both directions.
  (The original reaches the id by masking an address nibble and notes that a plain exclusive-or would
  have done.)
- **The jumping cossack also moves vertically** (`:4807–4810`, `:4831–4841`): when his id lands on the
  first frame his Y goes to `$67`, and on the second to `$5D` — he is drawn 10 pixels higher, which is
  the jump. The test is on the sprite id, so it fires only for that one pair of frames; the other nine
  performers toggle their sprite and stay where they are.

After the loop (`:4816–4829`): redraw (display work), then the exit test.

**The dance runs until the music stops.** The state reads back the song the sound driver reports it is
playing (`:4818`); while one is playing the state returns and animates again next frame. When the
driver reports none, the object buffer is cleared and the round leaves (`:4821–4828`):

| Start height | Next state |
|---|---|
| 5 | `$26` — the Buran launch |
| 0–4 | `$05` — the scoreboard |

### 5.1 The read-back is a query, not a call

The byte read at `:4818` is one of the six the sound driver shares with the game — the read-back slot
that reports the current song. A state handler has no route to the sound system, so the port takes it
as a supplied query: *is a song still playing?*

**With no query supplied the answer is "no", and the dance ends.** That default is deliberate and is
the safe one. The opposite default would hold this state forever on a build with no sound wired: the
animation would run, the exit would never fire, and the round would never finish. A dance that ends
early is a wrong screen; a dance that never ends is a stuck game.

---

## 6. What this unit does not do

Everything below is display work, specified with the renderer:

- Redrawing the piece sprites after they are hidden (`:4653–4654`).
- Redrawing the ten performers (`:4780–4781`, `:4816–4817`), including the whole body of the
  frame-timer-20 branch (§5).

The rule that decides each case is the same one the gameplay session uses: writes to the board are
simulation and are carried here; writes to video memory are display and are not. Both screens in this
unit are board writes and are therefore carried, tile for tile.

The wipe animation the two screen loads arm (§2) is stepped elsewhere — the line-clear work owns the
stepper; this unit only arms it.
