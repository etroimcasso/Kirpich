# Menu screens — behavioral contract

The pre-game selection screens: the config screen, the game-type and music-type selectors, and the
Type A / Type B difficulty pickers. Reverse-derived from the kaspermeerts/tetris disassembly
(`tetris.asm`, upstream `b95c668`). This is the authority `tests/test_menu_screens.cpp` checks the
port against; where the port and this document disagree, the port is wrong.

Ported in `src/systems/menu_screens.{h,cpp}` as free functions on the game-state aggregate. The
title and copyright screens (states `$06`/`$07`/`$24`/`$25`/`$35`) are a separate unit.

## Execution context

Each screen is one game state. The frame dispatcher reads the current state once per frame and runs
its handler; a handler may write a new state to transition on the next frame. Before the handler runs
the dispatcher has already sampled the joypad, so a handler reads the current frame's pressed/held
sets from the game aggregate rather than polling. After the handler runs the dispatcher decrements
the two frame timers, each saturating at zero. The selection handlers read **pressed** edges (the
original reads `hJoyPressed`, e.g. `:3598`); the difficulty inits run once on entry and set up the
screen.

State values (from the dispatch table): config `$08`, game-type select `$0E`, music-type select
`$0F`, Type A difficulty init `$10`, Type A level select `$11`, Type B difficulty init `$12`,
Type B level select `$13`, Type B height select `$14`. The bare `$09` slot is an unused `ret` state
(untouched here). `$0A` is the init-game state the pickers hand off to; `$15` is name entry.

## Cursor slots

Two sprite-object slots carry the cursors. Slot 0 (`$C200`) is the music-type cursor and the single /
first digit cursor; slot 1 (`$C210`) is the game-type cursor and the second digit cursor. A slot's
status byte `$80` hides it, `$00` shows it; the port models this as a `hidden` flag.

## Shared helpers

### `blinkCursor` — the blink half of `ReadJoypadAndBlinkCursor` (`:3597-3608`)

The original routine does two unrelated things: it returns the pressed set, and — only when the frame
timer is zero — reloads the timer to 16 and XORs `$80` into a cursor slot's status byte (the blink).
The port already delivers the pressed set through the joypad snapshot, so only the blink remains:
if `timer1 != 0` do nothing; else set `timer1 = 16` and toggle the slot's `hidden`. The 16-frame
cadence is driven by the dispatcher's per-frame timer decrement. The toggle is XOR, not set, so a
visibility write elsewhere in the same frame composes with it.

### `positionMusicTypeSprite` (`:3152-3172`)

Places the music-type cursor (slot 0). The current music-type value ($1C-$1F) indexes a coordinate
table; the slot's Y/X take that coordinate and the slot's sprite becomes the music-type value itself
(the four choices have consecutive sprite ids $1C-$1F). The top entry first cues the menu-move sound
($01 square SFX) — the original notes it may not audibly play; whether the driver consumes it is the
sound unit's concern, so the port writes the cue faithfully. The `.positionSprite` entry (`:3155`)
skips the cue and is used only by the two-player init (`:916`).

### `switchMusic` (`:3248-3256`)

Maps the music-type cursor value to an audio cue: value − $17 is the song id (5 = Type A, 6 = Type B,
7 = Type C); offset 8 (cursor $1F, "music off") maps to the stop-all cue ($FF) instead.

### `updateDigitCursor` (`:3574-3594`)

Places a digit cursor (given slot). The selected value indexes a coordinate table; the slot's Y/X
take that coordinate and the slot's sprite becomes the digit sprite for that value (digit 0 is sprite
$20, so sprite = value + $20). The top entry cues the menu-move sound ($01); the `.afterSFX` entry
(`:3579`) skips it and is used elsewhere (`:1210`/`:1214`). **Every selection-screen and
difficulty-init call site in this unit enters at the top (`:3330`, `:3384`, `:3420`, `:3424`,
`:3485`, `:3548`), so all of them cue the sound** — including the two difficulty-init screens, which
therefore each cue the menu-move sound on entry.

### `loadSceneSprites` — `LoadSprites` (`:3611-3628`)

Copies each scene record into consecutive $10-byte slots starting at slot 0, then hides the slot past
the last (the terminator: its status byte to $80). Each record fills the six modelled slot bytes:
status, Y, X, sprite, and the priority / flip attribute pair. The scene corpus never sets vertical
flip, so that clears to false. All call sites here start at slot 0.

### `clearOamObjects` — `ClearObjects` (`:3630-3638`)

Zeroes the entire 40-entry OAM staging buffer.

### `setStateAndShowCursor` — `Call_1675` / `Call_16E6` (`:3444-3448` / `:3508-3512`)

The two byte-identical helpers: write the next state, then unhide the cursor slot (status byte to
$00).

## `initConfigScreen` — `GameState_08` (`:3114-3148`)

Opens by resetting the serial hardware registers (interrupt-enable, serial data / control,
interrupt-flag, `:3115-3120`) — link-cable mechanism the serial unit owns, no simulation effect here.
Then `loadConfigScreenBody` (`.loadTiles`, `:3121-3148`), split out because the demo-start (`:617`)
and two-player (`:887`) paths enter it directly:

1. Clear the object buffer.
2. Load the two config-screen cursor sprites (`Data_26CF`) plus the terminator hide.
3. `positionMusicTypeSprite` (with cue): slot 0 gets the music-type coordinate and sprite.
4. The game-type cursor (slot 1): its X becomes the game-type value ($37 Type A / $77 Type B) and its
   sprite the matching label ($1C A-type / $1D B-type). Its Y and status come from the loaded record.
5. `switchMusic`.
6. Enter game-type selection (`$0E`).

Tile / tilemap loads and LCD-on are render mechanism, not modelled.

## `selectGameType` — `GameState_0E` (`:3258-3314`)

Cursor is slot 1. The game-type value doubles as the cursor's X coordinate.

- **Blink** slot 1.
- **Start** (`:3299`): cue the change-screen sound ($02); enter `$10` if game type is Type A else
  `$12`; unhide slot 1.
- **Confirm / A** (`:3312`): enter music selection (`$0F`); unhide slot 1.
- **Right** (`:3279`): if already Type B, no change; else set Type B, cue the menu-move sound, set
  slot 1's X to $77 and sprite to $1D.
- **Left** (`:3271`): if already Type A, no change; else set Type A, cue the menu-move sound, set
  slot 1's X to $37 and sprite to $1C.
- **Up / Down**: ignored (not tested by this state).

## `selectMusicType` — `GameState_0F` (`:3181-3246`)

Cursor is slot 0. The music-type value is a cursor tile in a 2x2 grid: `$1C $1D` / `$1E $1F`.

- **Blink** slot 0.
- **Start or Confirm / A** (`:3186-3189`): the shared advance path — cue the change-screen sound,
  enter `$10`/`$12` per game type, unhide slot 0.
- **Back / B** (`:3235-3246`): one-player returns to game-type selection (`$0E`) and unhides slot 0.
  Two-player is inert — the original falls through to the d-pad handling, and a Back press sets no
  direction, so nothing moves. (The two-player entry is reachable only via the two-player
  music-select subroutine.)
- **Grid moves** (`:3192-3233`), each repositions the cursor + updates the music + cues the sound:
  - Right: +1 unless the value is $1D or $1F (right edge).
  - Left: −1 unless the value is $1C or $1E (left edge).
  - Up: −2 if the value is ≥ $1E (bottom row), else no move.
  - Down: +2 if the value is < $1E (top row), else no move.

The upstream `; Bug!` at `:3201` (a redundant far jump) is behavior-identical — nothing to preserve.

## `initTypeADifficultyScreen` — `GameState_10` (`:3317-3342`)

1. Clear the object buffer. (The separate top-score-field clear is a render seam — no simulation
   effect.)
2. Load the one digit cursor sprite (`Data_26DB`).
3. `updateDigitCursor` (with cue) placing slot 0 at the current Type A level.
4. Refresh the Type A top scores (a seam; the draw-to-VRAM is render).
5. Enter Type A level selection (`$11`) — unless a top score was just earned (`newTopScore`), which
   routes to name entry (`$15`) instead; otherwise `switchMusic`.

Tilemap load and LCD-on are render mechanism.

## `selectTypeALevel` — `GameState_11` (`:3350-3400`)

Cursor is slot 0; a 2x5 grid over levels 0-9.

- **Blink** slot 0.
- **Start or Confirm / A**: enter the init-game state (`$0A`) **without unhiding the cursor** — the
  transition goes through `GameState_10.nextState`, which only writes the state. This asymmetry (the
  Type B pickers unhide; this one does not) is preserved.
- **Back / B**: enter the config screen (`$08`), also without unhiding.
- **Grid moves**, each repositions the cursor + refreshes the Type A top scores:
  - Right: +1 capped at 9.
  - Left: −1 floored at 0.
  - Up: −5 if the level is ≥ 5 (bottom row), else no move.
  - Down: +5 if the level is < 5 (top row), else no move.

## `initTypeBDifficultyScreen` — `GameState_12` (`:3408-3441`)

Same shape as the Type A init but with two cursors and no separate top-score-field clear (the Type B
refresh does its own):

1. Clear the object buffer.
2. Load the two digit cursor sprites (`Data_26E1`).
3. `updateDigitCursor` placing slot 0 at the current Type B level, then slot 1 at the current start
   height — each cues the menu-move sound.
4. Refresh the Type B top scores.
5. Enter Type B level selection (`$13`) — or name entry (`$15`) if a top score was earned; otherwise
   `switchMusic`.

## `selectTypeBLevel` — `GameState_13` (`:3450-3501`)

Cursor is slot 0; a 2x5 grid over levels 0-9. Each transition unhides the cursor (via
`setStateAndShowCursor`).

- **Blink** slot 0.
- **Start**: init-game (`$0A`), unhide slot 0.
- **Confirm / A**: Type B height selection (`$14`), unhide slot 0.
- **Back / B**: config screen (`$08`), unhide slot 0.
- **Grid moves** (identical to Type A level, over slot 0's Type B level coordinate table), each
  refreshes the Type B top scores.

## `selectTypeBHeight` — `GameState_14` (`:3514-3564`)

Cursor is slot 1; a 2x3 grid over heights 0-5. Each transition unhides the cursor.

- **Blink** slot 1.
- **Start or Confirm / A**: init-game (`$0A`), unhide slot 1.
- **Back / B**: Type B level selection (`$13`), unhide slot 1.
- **Grid moves**, each refreshes the Type B top scores:
  - Right: +1 capped at 5.
  - Left: −1 floored at 0.
  - Up: −3 if the height is ≥ 3 (bottom row), else no move.
  - Down: +3 if the height is < 3 (top row), else no move.

## Input vocabulary

The screens read a semantic action set: the four directions move a cursor, A confirms, B goes back,
Start starts/advances. Each menu action binds to the same physical source as its gameplay counterpart
(A ≡ rotate clockwise, B ≡ rotate counter-clockwise, the directions ≡ the movement / soft-drop
sources), with the up direction — otherwise unused in gameplay — taken by the menu up action. A single
press fires both the gameplay and menu action; this is harmless because a state runs only one of the
two handler families.

## Seams (not implemented here)

- The serial-register reset at the top of the config screen — serial / link-cable mechanism.
- `Update{TypeA,TypeB}TopScores` — the difficulty screens call these where the port takes a
  refresh hook; the staged rows are render-consumed, so there is no simulation effect in this unit.
  The top-score-entry unit wires the real refresh.
- `ClearTopScoreFields`, `DrawTopScoresToVRAM`, tile / tilemap loads, LCD control, and `RenderCursors`
  — render mechanism.
- The screen-load LCD-off → load → LCD-on sequence in each init: the screen identity is re-derivable
  from the game state, so the sim carries no effect; whether the intervening blank frames are
  presented is a later presentation decision.
