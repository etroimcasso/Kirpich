# Launch scenes — behavioral contract

The two bonus endings the game hides behind its hardest achievements: the Buran shuttle a player
earns by winning a Type B round started at garbage height 5, and the rocket a player earns by
scoring 100 000 points in Type A. Derived from `tetris.asm`; every behaviour below carries its
source line.

Both are pure spectacle — no input is read, no score changes, nothing the player does alters what
happens. Each is a fixed sequence of timed steps that builds a launch pad, holds, reveals smoke,
ignites, flies the vehicle off the top of the screen, and hands back to a screen that already
exists.

---

## 1. The states

| State | Handler | Source | Role |
|---|---|---|---|
| `$26` | `initBuran` | `:2694–2726` | Build the Buran pad, place the shuttle, start the music |
| `$27` | `prepareBuranLaunch` | `:2750–2762` | Reveal the two smoke plumes |
| `$28` | `buranIgnition` | `:2764–2782` | Hold on flicker, then swap the smoke art and clear the field |
| `$29` | `buranIgnition2` | `:2784–2803` | Hold on flicker, then swing the umbilicals away |
| `$02` | `buranLiftoff` | `:2805–2836` | Climb until the shuttle reaches the ignition height |
| `$03` | `buranRising` | `:2838–2872` | Fly off the top of the screen, then seed the congratulations cursor |
| `$2C` | `printCongratulations` | `:2874–2913` | Print sixteen letters, one per timed step |
| `$2D` | `congratulations` | `:2918–2929` | Restore the gameplay art and hand off to the scoreboard |
| `$34` | `gameOverToBonusEnding` | `:2931–2937` | Hold on the game-over screen, then enter the rocket chain |
| `$2E` | `initRocketLaunch` | `:2939–2959` | Build the rocket pad, place the earned rocket, start the music |
| `$2F` | `rocket` | `:2961–2973` | Reveal the two smoke plumes |
| `$30` | `rocketIgnition` | `:2975–2989` | Hold on flicker, then clear the field |
| `$31` | `rocketLiftoff` | `:2991–3022` | Climb until the rocket reaches the ignition height |
| `$32` | `rocketMainEngineFire` | `:3024–3054` | Fly off the top of the screen |
| `$33` | `endOfBonusScene` | `:3056–3065` | Restore the gameplay art and hand off to the title flow |

### 1.1 How a round reaches them

Neither chain is reachable in ordinary play; each is gated behind an achievement.

```
Type B, level 9, height 5 ──► $23 ──► $26 ─► $27 ─► $28 ─► $29 ─► $02 ─► $03 ─► $2C ─► $2D ─► $05
Type A, >= 100 000 points ──► $0D ──► $34 ─► $2E ─► $2F ─► $30 ─► $31 ─► $32 ─────────► $33 ─► $10
```

The Buran entry is the dance's exit fork (`:4818–4830`): when the jingle ends, a round started at
garbage height 5 goes to `$26` instead of to the scoreboard. The rocket entry is the game-over
chain's score test (`:4941–4970`): a solo Type A game that ends at 100 000 points or more records
which of three rockets was earned, sets a 144-frame hold, and writes `$34`.

Both chains rejoin screens that already exist: the Buran ends at the Type B scoreboard (`$05`), the
rocket at the Type A difficulty screen (`$10`).

---

## 2. The shared launch pad

`InitRocketLaunchGraphics` (`:2729–2748`) builds the part of the pad both scenes share. The
disassembly flags its name `; TODO name` — it is not rocket-specific, and `$26` calls it to build a
*Buran* pad.

In source order:

1. **Turn the display off** (`:2730`) — presentation, no simulation effect.
2. **Load the third tile set** (`:2731–2733`) — the multiplayer-and-Buran art. The copy length is
   `256*16`, which the disassembly itself annotates "Way too much"; the lasting effect is only which
   set is now current.
3. **Clear the second background map** (`:2734–2735`). `ClearTilemap` fills `$400` bytes *downward*
   from `hl` (`:6345–6354`), and it is entered with `hl = $9FFF`, so it clears `$9C00`–`$9FFF` — the
   whole second map. **It fills with `" "`, not with zero.** Through the charmap that is tile `$2F`.
4. **The backdrop block** at `$9DC0` (`:2736–2739`), four rows.
5. **The right tower's two sides** at `$9CEC` and `$9CED` (`:2740–2747`), seven cells each.

### 2.1 The two loop shapes, and why their names mislead

The pad is built with two different routines, and neither does what its name suggests.

- **`LoadTilemap.columnLoop` (`:6415–6431`) writes a two-dimensional block.** It is entered with
  `b` already set to a **row count**; each pass writes `SCRN_X_B` (20) cells left to right, then adds
  `SCRN_VX_B` (32) to return to the next row. Called with `b = 4`, it lays a 4-row × 20-column block.
- **`LoadTilemap9C00Row` (`:3101–3112`) writes a one-dimensional vertical strip.** Despite "Row" in
  its name it writes one cell, adds `$0020` to the destination, and repeats `b` times — `b` cells
  running *downward* at a fixed column.

The stored tower tilemaps agree: they are one-dimensional, seven entries each, ordered top to bottom.

### 2.2 Where every piece lands

Second-map addresses convert as `off = addr − $9C00`, `row = off / 32`, `col = off % 32`:

| Address | Arithmetic | Row, col | Content | Painted by |
|---|---|---|---|---|
| `$9DC0` | `$1C0 = 448 = 14·32 + 0` | 14, 0 | Backdrop block, rows 14–17 × cols 0–19 | shared pad |
| `$9CEC` | `$EC = 236 = 7·32 + 12` | 7, 12 | Right tower, left side, rows 7–13 | shared pad |
| `$9CED` | `$ED = 237 = 7·32 + 13` | 7, 13 | Right tower, right side, rows 7–13 | shared pad |
| `$9CE6` | `$E6 = 230 = 7·32 + 6` | 7, 6 | Left tower, left side, rows 7–13 | `$26` only |
| `$9CE7` | `$E7 = 231 = 7·32 + 7` | 7, 7 | Left tower, right side, rows 7–13 | `$26` only |
| `$9D08` | `$108 = 264 = 8·32 + 8` | 8, 8 | Umbilical `$72` | `$26` only |
| `$9D09` | `$109 = 265 = 8·32 + 9` | 8, 9 | Umbilical `$C4` | `$26` only |
| `$9D28` | `$128 = 296 = 9·32 + 8` | 9, 8 | Crew tunnel `$B7` | `$26` only |
| `$9D29` | `$129 = 297 = 9·32 + 9` | 9, 9 | Crew tunnel `$B8` | `$26` only |

The umbilical and crew-tunnel tiles are written as four individual cells (`:2704–2711`), not from a
stored table.

---

## 3. Both scenes draw on the second background map

The hardware keeps two background maps and displays one at a time. Everything these scenes draw goes
into the **second** map (`$9C00`), never the first.

Both entry states write `$DB` to the display control register (`:2718–2719`, `:2951–2952`), whose
bit 3 selects the second map; both exit states write `$93` (`:2926`, `:3061`), selecting the first
again. The display-off and display-on halves of those writes are presentation; **the map-select bit
is simulation** — it decides which grid is on screen.

The first map is never touched by either chain. A live playing field therefore survives underneath
both scenes and is still there when the game returns to it.

---

## 4. The Buran chain

### 4.1 `$26` — build the pad

Runs on its first frame with no gate. After the shared pad (§2):

- The left tower's two sides and the four umbilical/tunnel cells, per §2.2.
- The three launch objects — the shuttle and two smoke plumes — into sprite slots 0–2 (`:2712–2715`).
  The shuttle starts visible; both plumes start hidden.
- Select the second map (§3).
- Frame timer ← **187** (`:2720–2721`). The disassembly's own comment: "A hint over 3 seconds. Sigh".
- State ← `$27`; music ← `$10`, the launch theme (`:2724–2725`).

### 4.2 `$27` — reveal the smoke

Returns while the frame timer is non-zero. Then both smoke slots become visible (`:2754–2757`, the
"Launch smoke" comment), the timer reloads to **255** — the comment notes this is "4¼ seconds,
maximum possible" — and the state advances to `$28`.

### 4.3 `$28` — first ignition

While the timer runs, flicker the exhaust (§6) and return. On zero (`:2771–2782`):

- State ← `$29`.
- Both smoke slots take sprite `$35`, the second smoke frame (`:2774–2777`).
- Timer ← 255.
- **Fill the playing field with spaces and arm the wipe** (`:2780–2781`) — this is the shared
  gameplay helper, called with the space tile. It clears whatever round was underneath.

### 4.4 `$29` — swing the umbilicals away

Same flicker gate. On zero (`:2791–2802`): state ← `$02`, and the four umbilical/tunnel cells from
§2.2 are overwritten with space. Nothing else on the map changes — the towers and backdrop stay.

### 4.5 `$02` — liftoff

While the timer runs, flicker and return. On zero (`:2809–2831`):

- Timer ← **10** (the comment: "⅙ second").
- The shuttle's `y` decrements by one.
- **If the new `y` is not exactly `$58`, flicker and return.** The test is equality, not a threshold.
- At exactly `$58` (`:2816–2831`): smoke slot 1 becomes visible, its `y` becomes `$58 + $20 = $78`,
  its `x` becomes `$4C`, and its sprite becomes `$40` — the first Buran exhaust frame. Smoke slot 2
  is hidden. State ← `$03`; the noise channel is cued with `$04`, the flight sound.

The shuttle starts at `y = $5F`, so it takes seven of these steps to reach `$58`.

### 4.6 `$03` — rising

If the timer is non-zero, take the flicker tail (§4.7) and return. On zero (`:2842–2856`):

- Timer ← 10.
- The exhaust's `y` decrements, then the shuttle's `y` decrements.
- **If the shuttle's `y` is not exactly `$D0`, take the flicker tail.**
- At `$D0`: seed the congratulations cursor to `$9C82` (`:2851–2854`) and go to `$2C`.

### 4.7 The exhaust-frame tail

`$03`'s tail (`:2859–2871`) and `$32`'s (`:3041–3053`) are the same shape, and are **not** the shared
flicker of §6. When the second frame timer reaches zero it reloads to 6 and the *exhaust sprite's
id* is toggled by its low bit, alternating `$40`/`$41` for the Buran and `$5C`/`$5D` for the rocket.

### 4.8 `$2C` — the congratulations text

Returns while the frame timer is non-zero. Then (`:2878–2912`):

- Timer ← **6**, so one letter lands every six frames.
- The letter index is the cursor's low byte minus `$82`.
- The letter is written at the cursor cell, and tile `$B6` is written one row below it — the
  destination plus `$0020` (`:2895–2898`).
- The square channel is cued with `$02`, the screen-change sound — **once per letter**.
- The cursor advances one cell.
- **When the cursor's low byte reaches `$92`**, the timer is set to 255 and the state advances to
  `$2D`. The test is equality against the low byte only.

`$9C82` is second-map row 4, column 2; `$9C92` is row 4, column 18. So sixteen letters land at row 4,
columns 2–17, each with `$B6` beneath it at row 5. The stored sixteen-byte congratulations strip
matches the routine's own table (`:2916`) byte for byte.

`PrintCharacter` (`:4114–4122`) waits for the display to be between lines and then stores the byte;
the wait is hardware timing with no simulation effect, so each print is one map assignment.

### 4.9 `$2D` — hand back

Returns while the timer is non-zero. Then: restore the gameplay tile set, clear the line-clear list,
select the first map, and go to `$05` — the Type B scoreboard.

**`$2D` does not re-initialise the sound driver.** Its rocket-chain counterpart does. See §7.

---

## 5. The rocket chain

### 5.1 `$34` — the hold

The whole handler is a gate (`:2931–2937`): return while the frame timer is non-zero, then go to
`$2E`. The timer was set to 144 by the game-over chain before it wrote this state.

### 5.2 `$2E` — build the pad

Runs with no gate. After the shared pad (§2):

- The three launch objects into slots 0–2 (`:2941–2944`).
- **Slot 0's sprite is overwritten with the earned rocket tier** (`:2945–2946`), which the game-over
  chain recorded — one of three rockets by score.
- **That record is then cleared to zero** (`:2949–2950`), consumed.
- Select the second map; timer ← 187; state ← `$2F`; music ← `$10`.

**Asymmetry 1 — the rocket pad is sparser.** `$2E` paints no left tower and no umbilicals; it has
only what the shared pad builds. This is deliberate in the source: the handler goes straight from the
shared routine to placing its objects.

### 5.3 `$2F` — reveal the smoke

Gate; both smoke slots become visible; timer ← **160**; state ← `$30`.

### 5.4 `$30` — ignition

Flicker while the timer runs. On zero (`:2982–2988`): state ← `$31`, timer ← **128**, fill the
playing field with spaces and arm the wipe.

**Asymmetry 2 — `$30` sets no smoke sprites.** Its Buran counterpart `$28` swaps both plumes to the
second smoke frame; this one does not.

### 5.5 `$31` — liftoff

Structurally `$02` with different constants (`:2995–3017`): timer ← 10, the rocket's `y` decrements,
the sentinel is **`$6A`**, the exhaust lands at `y = $6A + $10 = $7A` with `x = $54` and sprite
`$5C`, smoke slot 2 is hidden, state ← `$32`, noise ← `$04`.

The rocket starts at `y = $6F`, five steps above the sentinel.

### 5.6 `$32` — main engine fire

Structurally `$03` (`:3028–3039`) with the terminal at **`$E0`** rather than `$D0`.

**Asymmetry 3 — `$32` seeds no cursor.** It forks straight to `$33`; the rocket chain has no
congratulations screen.

### 5.7 `$33` — hand back

**No timer gate at all** (`:3056–3065`) — unlike every other handler in either chain, it runs on its
first frame. Restore the gameplay tile set, **re-initialise the sound driver** (`:3059`), clear the
line-clear list, select the first map, and go to `$10` — the Type A difficulty screen.

---

## 6. The shared hold flicker

`Call_13FA` (`:3067–3086`) is the animation both chains run while waiting. It returns while the
second frame timer is non-zero; otherwise it reloads that timer to 10, cues the noise channel with
`$03` (the ignition sound), and toggles the visibility of **both smoke slots**.

Two details are contract:

- **The toggle is of the visibility byte.** The status byte is XORed with `$80`, and its domain is
  closed to `{$00, $80}`, so the operation is exactly "hide if visible, show if hidden".
- **The loop walks slots 1 and 2, not slot 1 twice.** It runs twice from `$C210`; the `ld l, $20`
  inside the body moves the destination to `$C220` on the first pass and is a no-op on the second
  (`:3075–3083`).

Called from `$28`, `$29`, `$02`, `$30` and `$31`. The `$03`/`$32` tails are a different animation
(§4.7).

---

## 7. The three asymmetries between the chains, preserved

The two chains are near-mirrors, and every place they differ is preserved rather than regularised:

| | Buran | Rocket |
|---|---|---|
| Pad | Right tower, left tower, four umbilical cells | Right tower only |
| Ignition state | `$28` swaps both plumes to smoke frame 2 | `$30` swaps nothing |
| After the climb | `$03` seeds a cursor → `$2C` congratulations → `$2D` | `$32` → `$33` directly |
| Sound driver on exit | `$2D` does **not** re-initialise it | `$33` does |
| Hold before reveal | 255 frames | 160 frames |
| Ignition hold | 255 frames | 128 frames |
| Climb sentinel | `$58` liftoff, `$D0` terminal | `$6A` liftoff, `$E0` terminal |

---

## 8. The climb runs past zero, and the wraparound is the mechanism

Both climbs decrement an eight-bit screen coordinate and test it for equality against a value
**above** where they started:

- The Buran runs from `$58` (88) down to `$D0` (208) — 88 steps to zero, one step wrapping to 255,
  then 47 more: **136 decrements**.
- The rocket runs from `$6A` (106) down to `$E0` (224) — 106, one, then 31: **138 decrements**.

This is the vehicle rising off the top of the screen and its coordinate wrapping, and it is the
intended behaviour. Because both tests are equality rather than a threshold, an implementation that
saturates at zero, or that uses a signed coordinate, never reaches the terminal and the scene runs
forever.

---

## 9. What these states do not do

- **They read no input.** Nothing in either chain samples the joypad; the sequences cannot be
  skipped, paused, or hurried.
- **They read no link-cable state.** Neither chain touches any serial byte.
- **They change no score, level, or line count.**
- **They never write the first background map or the board**, except through the shared field fill
  that `$28` and `$30` call, which clears the board and arms the wipe.

---

## 10. Presentation, not simulation

These are carried out by the display layer and have no simulation effect; each is recorded here so
that its absence from the handlers is deliberate rather than an omission.

| Behaviour | Source | Note |
|---|---|---|
| Display off | `:2730`, `:2922`, `:3057` | Paired with the enable below |
| Display on | `:2718–2719`, `:2926`, `:2951–2952`, `:3061` | Only the **map-select bit** is simulation (§3) |
| Tile copy | `:2733` | Lasting effect is only which set is current |
| Wait for the display between lines | `:4117–4120` | Inside the character print |

**Redrawing the objects is not on this list.** `RenderSprites` compiles the sprite slots into the
object buffer, and both chains call it with a count of three at seven sites — `:2717`, `:2827`,
`:2871`, `:2948`, `:3013`, `:3053`, and inside the shared flicker at `:3085`. Writing a slot and
compiling it are two different things, and a scene that wrote its slots without compiling them would
leave the vehicle off the screen while every slot read correctly. Each of the seven is a real call.

Two of the sites are conditional in a way worth stating: the tails at `:2859–2871` and `:3041–3053`
compile on every frame they run, including the frames where the second timer has not expired and no
sprite changed — but the frame on which each climb reaches its terminal returns without compiling
(`:2851–2857`, `:3037–3039`), because the next state redraws.

---

## 11. State the port carries for these scenes

One byte, and it is a third instance of a pattern the port already has twice.

**The congratulations print cursor.** `$03` seeds `$FFC9`/`$FFCA` with `$9C82` and `$2C` reads them
back each time it runs, advancing and storing (`:2851–2854`, `:2880–2912`). Progress through the
sixteen letters is recorded nowhere else, so it must be carried across frames.

The port carries the **column** only, in `GameFlowState::congratulationsColumn`: seeded to 2, terminal
at 18, letter index `column − 2`. The high byte `$FFC9` holds `$9C` for the whole sequence and is not
carried — the port already knows which map it is drawing on.

`$FFCA` is a **shared byte with two disjoint-in-time roles**, the same shape as `$FFC6`
(`coarseCountdown` / the name-entry column) and `$FFFB`/`$FFFC`. Its other role is the low half of
the top-score name-entry cursor, which the top-score screen recomputes each frame rather than
storing — that adjudication is unchanged and remains correct for that screen. The two screens cannot
run at the same time.
