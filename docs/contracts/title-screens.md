# Title and copyright screens — behavioral contract

Reverse-derived from `tetris.asm`. Governs `src/systems/title_screens.{h,cpp}` and
`tests/test_title_screens.cpp`. These are the pre-game states that run from power-on to the moment
the player picks a mode: the copyright chain, the title screen with its attract-demo countdown and
its one/two-player cursor. They are the entry the config screen (`docs/contracts/menu-screens.md`)
flows out of, and the attract loop the demo player is launched from.

Every handler is one game state — the frame dispatcher (`docs/contracts/dispatcher.md`) runs its
handler once per frame and reads the state index once, so a handler that writes a new state runs the
new handler on the *next* frame. The handlers are free functions on the game-state aggregate; they
own no state of their own. Addresses and line numbers below are `tetris.asm`.

The screens draw nothing themselves: loading tiles and tilemaps, turning the LCD on, and compiling
the sprite slots into the display are the renderer's job. The handlers mutate simulation state only —
the board, the sprite-object slots, the object buffer, the audio cue mailbox, and the game-flow
selections and timers. Points where the original loads graphics or toggles the LCD are marked
**render seam** and carry no simulation effect.

## Frame anatomy

Each handler runs inside the dispatcher's five beats (sample → dispatch → audio → soft-reset chord →
timers). The two frame timers `timer1`/`timer2` are decremented (saturating at zero) *after* the
handler returns, so a handler that reads `timer1 == 0` this frame, reloads it, and returns sees the
reloaded value decremented once before its next run. The copyright hold and the title countdown are
built on that: they reload a frame count and let the dispatcher's post-handler decrement drive it
down.

---

## The copyright chain: `$24` → `$25` → `$35`

Three states shown back to back at power-on, displaying the original owners' copyright notices. They
are preserved and shown verbatim: nothing renders without the player's own ROM supplying the tile
graphics, and the copyright tilemap shipped with the static screen data. The chain is a timed
display the player can skip.

### `initCopyrightScreen` — `GameState_24` (`:479–500`)

In order:

1. Disable LCD — **render seam**.
2. Load copyright/title tiles + the copyright screen tilemap — **render seam**.
3. `clearOamObjects` (`ClearObjects`, `:484`) — zero the 40-entry object buffer.
4. The piece-ring fill (below).
5. LCD on — **render seam**.
6. `timer1 = 250` (`4 * 60 + 10`, `:496`).
7. → state `$25` (`COPYRIGHT_SCREEN`).

#### The piece-ring fill and its over-copy (`:485–493`)

The original seeds the piece ring `wPieceList` from `DemoPieceList`, copying byte by byte until the
write pointer reaches `$C400`:

```
ld hl, wPieceList        ; $C300
ld de, DemoPieceList
.loop
    ld a, [de]
    ldi [hl], a
    inc de
    ld a, h
    cp a, $C4            ; stop when hl crosses into $C4xx
    jr nz, .loop
```

`wPieceList` is 256 bytes (`$C300`–`$C3FF`), but `DemoPieceList` is only 48 bytes. The loop copies
**256 bytes** — it over-reads 208 bytes past the end of the 48-byte table (whatever ROM data
follows the final `INCBIN`). The disassembly flags it: *"TODO, might copy way, way too much?
Including some music?"* (`:492`).

**The port copies the 48 real entries and drops the over-copy.** `pieceList[0..47]` receives
`kDemoPieceList` (`src/data/demo.h`); indices 48–255 are left untouched. The over-copied tail is
unreachable and never observed:

- Solo play draws pieces through the randomizer path (`docs/contracts/piece-random.md`), not the
  ring.
- A multiplayer round refills all 256 ring entries at `GameState_16` before use
  (`docs/contracts/piece-random.md` §4b).
- Attract-demo playback consumes only the low ring entries: demo 2 (Type A) plays piece indices
  0–15, demo 1 (Type B) plays 17–29, plus a one-piece preview lookahead — never past index ~30.

No reader ever reaches index 48 with the over-copied bytes live, so the tail is dead in the original
and absent in the port with identical observable behavior.

### `copyrightHold` — `GameState_25` (`:502–510`)

While `timer1 != 0`, return (the dispatcher's post-handler decrement counts it down). When
`timer1 == 0`: reload `timer1 = 250` and → state `$35` (`COPYRIGHT_SCREEN_SKIPPABLE`).

### `copyrightSkippable` — `GameState_35` (`:512–522`)

Advances to the title-screen init on either condition:

- **any input newly pressed** (`hJoyPressed` non-zero, `:513`), or
- **`timer1 == 0`** (`:516–518`).

Otherwise return. On advance: → state `$06` (`INIT_TITLE_SCREEN`).

**"any input" equivalence.** The original tests the whole `hJoyPressed` byte for any set bit. Every
physical Game Boy button binds to at least one port action (the input map binds all eight: the four
directions, A, B, Start, Select — `docs/contracts/input.md`), so "any bit of `hJoyPressed` set" is
exactly "any port action newly pressed". The port tests `joypad.pressed` for any action
(`pressed.bits() != 0`). The check reads the newly-pressed edge, not the held level — a button held
across from the previous frame does not skip.

---

## The title screen: `$06` init, `$07` loop

### `initTitleScreen` — `GameState_06` (`:524–580`)

Resets leftover game state from any prior round, paints the title board, seeds the 1P/2P cursor, and
arms the attract countdown. In order:

1. Disable LCD — **render seam** (`:525`).
2. **Field and pointer clears** (`:526–534`), all written `0`:

   | ROM operand | Address | Port effect |
   |---|---|---|
   | `hDemoRecording` | `$FFE9` | `demo.recording = 0` |
   | `[$98]` | `$FF98` | `flow.pieceLockStage = 0` |
   | `[$9C]` | `$FF9C` | `flow.blinkCounter = 0` |
   | `[$9B]` | `$FF9B` | **dead byte — no port field** (see below) |
   | `hTopScorePointerHi` | `$FFFB` | `flow.topOutLockCount = 0` |
   | `[$9F]` | `$FF9F` | `flow.lines = 0` (see below) |
   | `hWipeCounter` | `$FFE3` | `flow.wipeCounter = 0` |
   | `hNewTopScore` | `$FFC7` | `highScores.newTopScore = false` |

   Each operand resolves to exactly one owner via the shipped HRAM ownership census
   (`docs/contracts/game-state-machine-state.md`, `tests/fixtures/hram_expected.h`):

   - **`$FF98`** is the piece **lock stage** (`flow.pieceLockStage`) — written and read all through
     the lock process (`:5181`/`:5212`/`:5235` "start the locking process", `:5340` "lockdown stage
     3"; refCount 14 in the census). Clearing it here discards leftover lock state from a prior game.
   - **`$FF9B`** is a **dead byte**: written at several init/reset sites and at `:6057`/`:6064`
     (`"Never checked?"`), never read. It has no port field (the port models only live bytes), so
     clearing it has no port effect.
   - **`$FF9F`** is the **high byte of `hLines`** — a two-byte packed-decimal count whose high-byte
     raw accesses (`:5837`, `ld de, hLines + 1`) are same-field accesses, not a separate field
     (`docs/contracts/game-state-machine-state.md`). The original clears only the high byte here;
     `ClearScoreAndStats` (`:6191`, called just below) clears `wLineClearStats` + `wScore` in work
     RAM and does **not** touch `hLines`. The port's `lines` is one decimal field and cannot
     represent a partial-BCD-byte clear, so it clears the whole field: `flow.lines = 0`. This is
     observably equivalent — nothing between this init and the next game-init reads `hLines` (its
     only reads, `:5775`/`:5845`, are the Type-A max-out bonus checks in gameplay), and game-init
     rewrites `hLines` in full (`:1259`/`:4190`).

3. `clearLineClearsList(game)` (`ClearLineClearsList`, `:535`) — the shipped line-clear reset.
4. `clearScoreAndStats(game)` (`ClearScoreAndStats`, `:536`) — the shipped scoring reset (score +
   line-clear tallies).
5. Load copyright/title tiles — **render seam** (`:537`).
6. **Board paint** (`:538–555`), on the 32×32 board (`docs/contracts/playing-field-state.md`):
   - Fill the whole page `$C800`–`$CBFF` (all 1024 cells) with the empty tile `$2F`
     (`CharTile::SPACE`) — `:538–544`.
   - Wall columns via `Call_26A9` (`:6255–6264`): 32 rows, `$20` stride, tile `$8E`. Called at
     `$C801` (board column 1) and `$C80C` (board column 12) — `:545–548`. These are the left and
     right walls bracketing the 10-wide visible field (columns 2–11).
   - Floor: 12 cells of `$8E` at `$CA41` — board row 18, columns 1–12 — `:549–555`.
7. Load the title-screen tilemap — **render seam** (`:556–557`).
8. `clearOamObjects` (`ClearObjects`, `:558`) — zero the object buffer.
9. **1P/2P selector cursor** (`:559–564`): `oam[0] = { y = $80, x = $10, tile = $58 }`. (The clear
   above zeroed the attributes; only Y/X/tile are set.)
10. `audioCues.music = MusicId::TITLE` (`wNewMusicID = 3`, `:565–566`).
11. LCD on — **render seam** (`:567–568`).
12. → state `$07` (`TITLE_SCREEN`); `timer1 = 125` (`$7D`, `:569–572`).
13. **Attract countdown seed** (`:573–579`): `coarseCountdown = 4`; then if `hDemoNumber` (`$FFE4`,
    `demo.activeDemo`) is `0` (`ActiveDemo::NONE`), `coarseCountdown = 19` (`$13`). Four between
    attract demos (a demo just ended, so `activeDemo` is non-zero); nineteen on a cold entry (no
    demo has run).

### `titleScreen` — `GameState_07` (`:632–731`)

Three concerns each frame: the attract countdown, the (deferred) serial poll, and the cursor/input.

#### Attract countdown (`:633–640`)

When `timer1 == 0`: decrement `coarseCountdown`; if it reaches `0`, launch the attract demo (below);
otherwise reload `timer1 = 125`. When `timer1 != 0`, skip straight to the input handling. (When the
countdown decrements a `coarseCountdown` already at `0`, it wraps to `255`, non-zero, so the demo
does not launch that frame — matching the `dec`/`jr z` semantics.)

The demo launch is the `StartDemo` routine (`:582–630`), which belongs to the demo unit. The port
exposes it as a seam — a `StartDemoHook` fired when the countdown reaches zero (`:638`). Until the
demo unit installs the real hook, the default is a no-op: a cold port idles at the title (`timer1`
stays `0`, so the next frame decrements `coarseCountdown` again — wrapping past zero and re-arming
`timer1 = 125` on the following frame). The hook consumes the config-screen body from the menu unit.

#### Serial poll (`:642–655`) — deferred

`DelayMillisecond`, the slave-mode `rSB`/`rSC` setup, and the serial-interrupt check that launches a
peer-initiated two-player game (→ state `$2A`) or resets the cursor to 1P are link-cable mechanism.
They are driven by `hSerialInterruptTriggered`/`hSerialRole`, which nothing sets without the serial
subsystem. **Deferred to the serial/multiplayer unit; no simulation effect here** — with no peer,
the check always falls through to the input handling.

#### Cursor and input (`:657–731`)

Reads the newly-pressed edge. `multiplayer.isMultiplayer` doubles as the cursor index (`:660`
*"(Ab)used here to keep track of the pointer"*) — the 1P/2P selector position and the link-mode flag
are the same byte. Buttons are tested in this order; the first match handles the frame and returns:

- **Select** (`:661–662`, `:708–718`): toggle the cursor (`isMultiplayer = !isMultiplayer`) and
  place it — `oam[0].x = isMultiplayer ? $60 : $10`.
- **Right** (`:663–664`, `:720–724`): move 1P → 2P **only** (no-op when already 2P); sets the
  cursor as Select does.
- **Left** (`:665–666`, `:726–731`): move 2P → 1P **only** (no-op when already 1P).
- **Start** (`:667–706`):
  - **1P** (`isMultiplayer == false`, `:669–671`, `:698–706`): the heart-mode check first — if
    **Down is held** (`hJoyHeld`, `bit PADB_DOWN`, `:700–701`), set heart mode; then transition to
    the config screen with the entry zeroing (below). Heart mode is `flow.heartMode`, a `uint8_t`
    read only as zero/non-zero (`docs/contracts/game-state-machine-state.md`): the original stores
    the raw held byte (non-zero because Down is set), so the port stores a canonical non-zero flag
    (`1`) — the specific value is never examined, and the port does not reconstruct a joypad byte.
    Down is the `SoftDrop` source (also `MenuDown`; same physical button).
  - **2P** (`isMultiplayer == true`, `:672–687`): the master-election serial handshake — becomes
    master, waits for the serial interrupt, and transitions to state `$2A` with the same zeroing.
    This is link-cable mechanism, **deferred to the serial/multiplayer unit**; the port takes no
    action on a 2P Start in this unit (there is no cable without the serial subsystem, and the
    transition is gated on the handshake result). The pre-filter `cp a, PADF_START` (ignore Start
    when other buttons are also pressed, `:673–674`) is part of that deferred path.

**The Start entry zeroing** (`.nextState`, `:688–696`), applied on the 1P transition: state → `$08`
(`INIT_TYPE_SELECTION`); `timer1 = 0`; `typeALevel = 0`; `typeBLevel = 0`; `typeBStartHeight = 0`;
`demo.activeDemo = NONE` (leaving the attract chain).

---

## White-blank (LCD-off → load → LCD-on)

Every init handler in this family (`$24`, `$06`) and in the menu family (`$08`, `$10`, `$12`) runs
LCD-off → load graphics → LCD-on. The simulation carries no effect from it — the screen identity is
re-derivable from `flow.gameState`. Whether the blank frames between LCD-off and LCD-on are
*presented* is a later presentation decision (cross-referenced in `docs/contracts/menu-screens.md`).
The contract pins the transition points; there is nothing to simulate.

---

## Deferred to later units

| Behavior | Site | Owner |
|---|---|---|
| `StartDemo` attract launch | `:582–630`, fired at `:638` | demo unit (via `StartDemoHook`) |
| Serial poll / peer-launch | `:642–655` | serial/multiplayer unit |
| 2P Start master election | `:672–687` | serial/multiplayer unit |
| LCD off/on presentation | init handlers | presentation work |
