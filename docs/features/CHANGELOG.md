# Features Changelog

Chronological log of feature status transitions, newest first. Entries are not edited after they
are written. `../FEATURES.md` holds current status; this file holds history.

**Format:** `<feature> <old status> → <new status>` with a one-line reason. Glyphs match
`../FEATURES.md`: ⬜ pending · 🟡 in progress · ✅ delivered · 🟫 dropped · ❌ rejected.

---

## 2026-08-15

- **Game-state dispatcher** ⬜ → ✅. Every frame of the game is one pass of a dispatcher — sample the
  joypad, dispatch through a 54-entry table on the current game state to that state's handler, tick
  the sound driver, check the four-button soft-reset chord, decrement two frame timers.
  `GameStateDispatcher::tick` (`src/systems/game_state_dispatcher.{h,cpp}`) is that per-frame body
  (the engine run loop drives it, one call per sim tick; there is no port-side loop). The dispatch
  index is read once before the handler runs, so a handler that writes a new state transitions the
  next tick. The chord (Start + Select + A + B held) fires a soft-reset seam and skips the timers,
  extras don't block, and it re-fires every tick it is held. Handlers are `std::function` slots the
  systems fill in as they land; every slot ships as a stub that leaves the state untouched, so an
  unported state sits in place. `GameContext` (`src/systems/game_context.h`) aggregates the seven
  ported state structs plus the joypad snapshot as the one argument every handler binds against. Adds
  `Start` / `Select` to the action vocabulary (the chord's first consumer). First systems-layer
  framework after the input layer; ships zero real handlers by design.

- **Input** ⬜ → ✅. Every input flows through one per-frame mechanism — poll the joypad once, pack
  the buttons into a held set, derive the rising edge (`pressed = held & ~previouslyHeld`). Ported
  over the engine's action-input system as `InputSystem::sample` (`src/systems/input.{h,cpp}`), which
  samples held levels and derives the edge itself so a sub-tick tap is dropped exactly as the
  once-per-frame poll drops it — the engine's never-drop-a-press signal is deliberately unused. One
  `sample` call serves both live play and the demo playback's substituted held set. The shared
  key-repeat (DAS) core `keyRepeatFire` (press fires and arms 23; held counts down and fires on 9;
  stale-zero wraps to a 255-frame delay) is here; the per-site parts (idle re-arm, wall-charge retry,
  direction priority) stay with the piece and name-entry systems. `defaultActionMap` binds the five
  piece actions to keyboard + gamepad. No new action enumerators; the consumer sites mint theirs as
  they land. First systems-layer surface after the randomizer.

- **Randomization** ⬜ → ✅. Every random piece flows through one mechanism — read the free-running
  divider, fold the byte into a piece kind, reject a repeat (up to three tries, the third accepted
  unconditionally). The fold is a port-owned SM83 routine (`src/vm/random.asm`) hosted on the
  engine's virtual machine: the fold burns cycles that advance the divider *within* a call, so a
  retry sees a fresh byte — a native bare read would freeze it and skew the distribution. The
  rejection loop and the one-stage piece pipeline (an accepted candidate enters the pipeline one call
  before it is played) are native C++ (`src/vm/piece_random.{h,cpp}`, `pickRandomPiece`). The solo
  per-piece draw (`NextPiece.randomChoice`) reuses this same draw core when the piece system lands;
  recorded in the contract. The first game-logic surface.

## 2026-08-14

- **Playing-field state** ⬜ → ✅. The board the game plays on — the 32×32 background-map shadow the
  original keeps at `$C800` (the authoritative field that collision reads, piece locking writes, line
  clears scan, and garbage fills) and the 10-cell `$C400` multiplayer attack staging row — ported as one
  hand-written `PlayingFieldState` struct (`src/state/playing_field_state.h`). The board carries the whole
  32×32 grid, not just the visible 18×10 field, because the walls, the floor, the below-floor garbage, and
  the startup clear all reach the whole page; cells are raw tile indices; a `fieldCell(row, col)` accessor
  reaches the visible field by its own coordinates. One hand-entered wire scalar (`kAttackRowBrickTile` =
  `$28`); the video-RAM copy the original mirrors as it writes is render-bridge mechanism, not state. No
  parser work — the fifth state unit checked entirely against the existing work-RAM census and playing-field
  wipe fixtures. State only; collision, locking, the line-clear pipeline, the wipe, the fills, the overlay
  screens, and the multiplayer attack machinery are later work. +6 tests (135 → 141). See
  [`../contracts/playing-field-state.md`](../contracts/playing-field-state.md).
- **High-score state** ⬜ → ✅. The top-score surface — the two high-score tables `wTypeBTopScores`
  (`$D000`, indexed by level/starting-height/rank) and `wTypeATopScores` (`$D654`, level/rank), plus the
  four high-RAM bytes the score-entry flow uses — ported as one hand-written `HighScoreState` struct with
  a `TopScoreEntry` cell type (`src/state/high_score_state.h`). Each score is a decimal `uint32_t` (BCD only
  on the wire); each name is six `CharTile` glyphs; `newTopScore`/`topScoresRedrawRequested` are `bool`,
  `newScoreRank` keeps the original's inverted rank (3 = 1st), and `nameEntryColumn` is an overlay field on
  a byte the game-flow state owns as `coarseCountdown` (disjoint in time). Adds the **first port-side
  durable state**: a wire codec (struct ↔ the 1890-byte table image) and `load`/`save` through the engine's
  `retropp::SaveStore` (`src/state/high_score_persistence.{h,cpp}`) persist top scores across launches,
  always on — strictly extending the original's soft-reset-only survival; the in-sim tables are unchanged.
  Save identity `Kirpich`/`Kirpich`; a corrupt save runs with no scores and is left in place. State + codec
  only — the insert/name-entry/display mechanisms and the game-loop wiring are later work. +7 tests
  (128 → 135). See [`../contracts/high-score-state.md`](../contracts/high-score-state.md).
- **Demo state** ⬜ → ✅. The state the attract-mode demo carries between frames — which demo is running,
  the dead recording flag, the run-length countdown, the cursor into the active input timeline, and the two
  held-button sets (the demo's own held buttons and the player's real held state parked while the demo
  drives) — ported as one hand-written `DemoState` struct plus a small `ActiveDemo` enum
  (`src/state/demo_state.h`): the seven demo-machinery bytes in the original's high RAM (`$FFE4`,
  `$FFE9`–`$FFEE`). `activeDemo` is `ActiveDemo` (`NONE`/`TYPE_B`/`TYPE_A`, carrying the game type; the
  numbering is inverted against play order — `TYPE_A` plays first); `recording` stays `uint8_t` because its
  enable value is `$FF` and consumers split between `== $FF` and any-non-zero; the two pointer halves
  collapse into one `uint16_t nextRecord` record index; the two held bytes are `retropp::ActionSet`, the
  same action vocabulary a `DemoInputRecord` carries. State only — the playback loop, the pressed-edge
  derivation, the RLE decode/encode, the demo alternation, the end-of-demo checks, and the save/restore
  substitution are the demo systems and are recorded with anchors in the contract. **Third state unit with
  no parser work:** the high-RAM layout+census fixture already carries the seven labelled rows and a census
  entry for the two raw-accessed bytes (`$FFE4`, `$FFED`); the shipped `$FF80` ownership guards already
  assign these bytes here and need no change. Delivers `src/state/demo_state.h`,
  `tests/test_demo_state.cpp` (5 tests, baseline 123 → 128), the contract
  (`contracts/demo-state.md`), and the feature + engine docs.

## 2026-08-13

- **Serial / multiplayer state** ⬜ → ✅. The state two Game Boys share over the link cable — the
  master/slave role and the serial protocol bytes, the polymorphic in-round status byte (stack height /
  `$80|rows` garbage attack / round-end code), the received-garbage pipeline, the match win tally, the
  dead advantage/deuce display path, and the pause save slots — ported as one hand-written
  `MultiplayerState` struct plus a small `RoundOutcome` enum (`src/state/multiplayer_state.h`): twenty-six
  bytes scattered through the original's high RAM (`$FFAC`–`$FFF2`), gathered by purpose. Bytes read only
  as zero / non-zero become `bool`; `role`/`protocolState` reuse the generated `SerialRole`/`SerialState`
  enums; `roundOutcome` is minted here (`NONE`/`WE_LOST`/`WE_WON`, the inverted `$77`/`$AA` wire codes);
  `tx`/`rx` stay raw because they also move board and piece-list payload; `transferCompleted` stays
  `uint8_t` because two upstream bug sites write `$1B`/`$1F` into it. **Second state unit with no parser
  work:** the high-RAM layout+census fixture already carries the twelve labelled rows, the five gap rows
  the fourteen unlabelled bytes fall in, and a census entry for each; the shipped `$FF80` ownership guards
  already assign these bytes here and need no change. Delivers
  `contracts/serial-multiplayer-state.md` (field map, the full two-player protocol narrative, the wire-code
  vocabulary, the garbage pipeline, the dead-trio adjudication, and the four preserved quirks) and
  `test_multiplayer_state.cpp` (HRAM window pins, per-byte field resolution with the `$FFDD`–`$FFE0`
  negative guard, reset/boot pins, wire-value pins). Baseline 118 → 123; parser unchanged at 607.

- **Sprite renderer state** ⬜ → ✅. The live sprite-object array the original keeps at `$C200` — sixteen
  `$10`-byte slots every menu cursor, gameplay piece, and scripted scene writes and the renderer compiles
  into the OAM staging buffer — ported as one hand-written `SpriteRendererState` of `SpriteSlot`s
  (`src/state/sprite_renderer_state.h`): each slot a `hidden` flag, screen `y`/`x`, the composed-sprite
  `spriteId` (which for a piece is its rotation state), the three attribute bytes unpacked to
  `behindBg`/`yflip`/`xflip`/`palette1`, and the ending dancers' `animCounter`/`animReload` pair; the
  seven never-accessed slot bytes are dropped, and `kActivePieceSlot`/`kPreviewPieceSlot` name slots 0/1.
  The renderer's own working memory (`$FF86`–`$FF97`, `$FF94`, `$FFB2`–`$FFB5`) is call-transient
  mechanism, adjudicated in the contract and re-implemented with locals by the later render bridge — not
  carried as state, the same treatment the audio unit gave the sound driver's RAM. **First state unit
  with no parser work:** both existing fixtures already carry every row it consumes — the work-RAM census
  (twelve `$C2xx` rows) and the high-RAM layout+census (the sprite-renderer HRAM window). Delivers
  `contracts/sprite-renderer-state.md` (slot byte map, HRAM mechanism table, entry-point OAM bases,
  escape semantics, slot-role inventory) and `test_sprite_renderer_state.cpp` (census-offset sweep with
  the dropped-offset guard, HRAM window pins, slot-shape and reset pins, `SpriteId` links). Baseline
  113 → 118; parser unchanged at 607.

- **Audio state** ⬜ → ✅. The `$DF70`–`$DFFF` block of work RAM where the original sound driver keeps
  its state — cue mailboxes, the pause command and music read-back, and the driver's private working
  memory. Unlike every other state unit this one ships **no C++ struct and no `src/` file**: the port
  plays audio by running the ROM's own sound driver as embedded code on the engine's virtual sound CPU,
  so those bytes are the port's state inside that machine's RAM, never mirrored into a second copy. The
  unit delivers the **boundary contract** (`contracts/audio-state.md`) — every byte of the window
  adjudicated as a cue lane, a slot, or driver-private, with all game-side access sites anchored — and a
  **census guard** that proves it: the work-RAM layout parser (`tools/asm_parser/parse_wram.py`) gained a
  second pass scanning `tetris.asm` for every static WRAM operand into a `{address, refCount}` table, and
  `tests/test_audio_state.cpp` resolves every censused byte across all of `$C000`–`$DFFF` to exactly one
  owner. The audio window contains exactly six game-reachable bytes; every driver-private byte is proven
  private by its absence from the game-side census. The cue lanes carry the existing id sets (`MusicId`,
  `SquareSfxId`/`NoiseSfxId`/`WaveSfxId`); no new types. Baseline 109 → 113; parser 590 → 607.

- **Game-state-machine state** ⬜ → ✅. The original's `$FF80` high-RAM globals that the main loop lives
  in, ported as one hand-written `GameFlowState` struct (`src/state/game_flow_state.h`): the `gameState`
  dispatch index, the frame/wipe/drop timers, the level and menu selections (`gameType`/`musicType` as
  the existing enums), the piece-pipeline counters, and `reset()` to the boot state — 27 fields in all.
  `lines` is a decimal `uint16_t` (packed decimal only on the wire), `paused` is a `bool` and `heartMode`
  a `uint8_t` (widths traced from how the original writes each), and two bytes are shared disjointly in
  time with the top-score pointer — `tempPreviewPiece` (`$FFFC`) and `topOutLockCount` (`$FFFB`, the
  topout piece counter that forces game over), each an independent field here. Four live bytes the
  disassembly leaves unlabelled become named fields (`pieceLockStage`, `blinkCounter`, `completedRowCount`,
  `coarseCountdown`). A new whole-file high-RAM parser (`tools/asm_parser/parse_hram.py`) walks
  `hram.asm` into a `{name, address, size}` layout fixture and scans `tetris.asm` for every raw-operand
  high-RAM access into a `{address, refCount}` census; `tests/test_game_flow_state.cpp` tiles the layout,
  pins the field widths, and resolves every censused byte to exactly one owner so no raw-accessed byte is
  silently unowned. The mapping and the per-byte ownership of the whole map are in
  `contracts/game-state-machine-state.md`. Baseline 103 → 109; parser 550 → 590.

## 2026-08-12

- **Global game state** ⬜ → ✅. The original's `$C000` work-RAM globals ported as one hand-written
  `EngineState` struct (`src/state/engine_state.h`): the 40-entry `OamEntry` sprite staging buffer (the
  DMG object-attribute byte unpacked into named flags), the decimal `score` and its `LineClearStats`
  tallies, the four-entry `lineClears` bounded vector of field-row indices, the soft-drop points, the
  scoreboard/preview flags, and the 256-entry `Piece` ring — plus `reset()` to the boot state. The score
  is decimal (the packed-decimal shadow and the soft-drop scratch copy are not stored), the line-clears
  are row indices rather than addresses, and three flags the disassembly never labels take role names
  anchored to their use sites in `contracts/engine-state.md`. A new whole-file RAM-layout parser
  (`parse_wram.py`) emits the `{name, address, size}` fixture the width tests pin against;
  `tests/test_engine_state.cpp` proves the layout tiles each RAM section and checks the struct widths,
  `reset()`, and the value types. Also added `engine/engine-state.md` and this feature doc, and added the
  playing-field shadow-state row to the state registry.

- **Miscellaneous data** ⬜ → ✅. The loose tables and constants collected into one header
  (`src/data/misc.h`): four raw sprite-object tables (25 `OamObject`s — the two-player face pairs and
  the PUSH-START prompt, drawn directly rather than through the composed sprites), six cursor
  coordinate tables (42 `SpriteCoordinate` pairs — the Type-A/Type-B level pickers, the Type-B and
  two-player start-height pickers, and the music-type picker), the four win-screen strings (raw
  gameplay-tileset bytes) plus "pause" (a `CharTile` array), and three constants (the demo-recording
  sentinel and the completed-row scan's first-row/row-count pair). The three level/start-height cursor
  tables the disassembly leaves unnamed take role names (`kTypeALevelCursorCoordinates`,
  `kTypeBLevelCursorCoordinates`, `kTypeBStartHeightCursorCoordinates`); the music-type table is read
  through `musicTypeSpriteCoordinate`, which maps the music-type value to its index. Generated by
  `parse_misc.py`; swept in full by `tests/test_misc.cpp`. Also added `engine/misc.md` and this
  feature doc, and completed the engine doc index (`engine/README.md`) with the demo and misc pages.

- **Demo data** ⬜ → ✅. The two attract-mode demo recordings and the shared piece list — the Type A
  (128) and Type B (80) `DemoInputRecord` timelines, each step a `retropp::ActionSet` of held game
  actions plus a frame count, and the 48-entry `Piece` sequence both demos replay — ported as the
  header-only `src/data/demo.h`. The recordings capture Game Boy joypad state; the port resolves each
  button bit to the game action the gameplay input handler binds it to (A → rotate clockwise, LEFT/RIGHT
  → shift, DOWN → soft drop), carried in the game's own `Action` vocabulary (`include/kirpich/action.h`,
  minted here). So the records hold engine action sets the demo-replay system feeds into the input path,
  not hardware bytes — and no port-side button type is invented. The demo blobs are algorithmic data and
  commit into the binary. `tests/test_demo.cpp` sweeps both timelines by bridging each raw fixture byte
  to its action set (a wrong button-to-action mapping fails), sweeps the piece list's spawn-orientation
  domain, and pins the corners and consumed index ranges; the encoding, the mapping with source anchors,
  and the copy-overrun quirk are in `contracts/demo.md`.

## 2026-08-09

- **Sound-effect data** ⬜ → ✅. The three effect-ID spaces the game triggers sound effects by — the
  `SquareSfxId` (8), `NoiseSfxId` (4), and `WaveSfxId` (2) enums, each the wire byte the game writes to
  an audio-state variable, plus `NONE` — and the constants locating the four SFX pointer tables, ported
  as the header-only `src/data/sfx.h`. Every effect and driver data blob (register images, envelope and
  frequency ramps, the note-frequency table, vibrato offsets, the noise-note table, the five wave
  timbre patterns, and the pause-tune notes) is mechanical configuration and is pinned as raw bytes in
  the fixture, checked against the player's ROM cell for cell. Every blob's address — including the ones
  embedded between driver code — is computed by walking instruction lengths from the audio section
  origin, with the disassembly's address-encoding labels as checkpoints, so nothing is assembled.
  Hosting the driver to actually play the effects is the audio work and builds on this data.
  Full-corpus sweep in `tests/test_sfx.cpp`; the tables, dispatch, and quirks in `contracts/sfx.md`.

## 2026-08-08

- **Music data** ⬜ → ✅. The identifiers and address map for the game's 17 songs — the `MusicId`
  enum (the wire byte the game selects a song by, plus the `NONE`/`STOP` sentinels) and the constants
  locating the song/channel/section graph, the per-song stereo table, and the note-length tables —
  ported as the header-only `src/data/music.h`. The song sequences themselves are copyrightable
  musical content and are never committed: each section is pinned by `{address, length, SHA-1}` and
  the test recomputes the hash from the player's ROM. `StereoData` and the note-length region are
  mechanical config and are pinned as raw bytes. The parser reconstructs the whole graph and requires
  it to tile `[0x6F3F, 0x7FC6)` exactly; `kStereoDataAddr` is the one hand-entered address, guarded by
  a ROM read. Hosting the driver to actually play the music is the audio work and builds on this map.
  Full-corpus sweep in `tests/test_music.cpp`; grammar and driver behavior in `contracts/music.md`.

- **Garbage-fill tables** ⬜ → ✅. The garbage a Type B game starts buried under — the fixed 4 × 10
  table the attract-mode demo stamps, and the constants the procedural fill and its three start paths
  consume (rows per Type B height, multiplayer round-start rows, the eight-tile block range, and the
  empty tile) — ported as the header-only `src/data/garbage.h`. Cells stay raw `uint8_t` tile indices
  (the tilemaps precedent), swept in full against a flat 40-byte fixture; the empty tile is shown to
  equal the character map's space glyph. The procedural fill itself, the demo stamp, and the
  multiplayer garbage attack are gameplay/serial logic and port later — their write addresses and the
  fill's mechanism are recorded in `contracts/garbage-init.md`. Full-corpus sweep in
  `tests/test_garbage.cpp`.

- **Sprite scene lists** ⬜ → ✅. The sprite objects each scripted scene places on screen — the
  two-player victory and defeat characters, the ending dance troupe, the Buran and rocket launches,
  the config/difficulty/height menu markers, and the active- and preview-piece templates — ported as
  13 object tables (35 objects) in the header-only `src/data/scene_sprites.h`. One `SceneSprite`
  type (`{ hidden, y, x, sprite, behindBg, xflip }`) serves both record shapes; the eleven scene
  lists return `std::span<const SceneSprite>` and the two piece templates return a reference. Each
  object's sprite is a `SpriteId` from the sprites unit; the three attribute bits that vary across
  the corpus unpack to named bools and the invariant bits are checked at generation. The raw OAM
  face/push tables and the coordinate tables stay with the miscellaneous object data. Renamed from
  the placeholder "Sprite tile data and sprite lists" registry row (the tile data itself is the
  sprites unit). Full-corpus sweep in `tests/test_scene_sprites.cpp`.

## 2026-08-07

- **Sprite rotation tables** ⬜ → ✅. Every multi-tile sprite the game draws — the 28 piece
  rotations, the game-type and music-off labels, the ten score digits, the Mario/Luigi
  victory-and-defeat characters, the Buran shuttle and rockets with smoke and exhaust, and the
  ending-dance musicians — ported as one composed record per identity in the header-only
  `src/data/sprites.h`: a 94-value `SpriteId` enum (`include/kirpich/sprite_id.h`), the `kSprites`
  table of `{ id, offset_y, offset_x, parts }`, and the `getSprite` accessor, with parts held in a
  new `BoundedVec<SpritePart, 28>` (`src/data/bounded_vec.h`). The parser resolves each identity
  through its record, tile list, and grid and walks the `$FF`/`$FE`/`$FD` escape encoding to compose
  the parts; the four aliased identities carry a full copy of the layout they repeat. The **sprite
  layout grids** surface (`SpriteGridOffset`, the five `kSpriteGrid*` arrays, and their unit) is
  folded in — the grids are now internal parser input and raw fixture pairs, and `PieceKind` moves to
  the sprite unit; the grids' feature and contract records are retained/absorbed. The **scoring**
  bonus-ending rocket bytes are retyped from raw `uint8_t` to `SpriteId`. Test baseline 61 → 63;
  parser suite 269 → 300. See [`sprite-oam-rotations.md`](sprite-oam-rotations.md),
  [`../contracts/sprites.md`](../contracts/sprites.md), [`../engine/sprites.md`](../engine/sprites.md).

- **Tilemaps** ⬜ → ✅. The 22 background-tilemap screens — the nine full screens, the three
  banner strips, the two playing-field overlays, the three window messages, the four tower columns,
  and the congratulations strip (~4.1 KB of tiles) — ported as composed row-major grids of raw
  `uint8_t` tile indices in the header-only `src/data/tilemaps.h`, alongside four dimension
  constants read from the loaders. Text rows decode through the character map by the same greedy
  longest-match the assembler uses, so the `.”` ligature stays one tile; the field overlays' `$FF`
  copy-terminator is dropped from the composed grid and kept in the byte fixture; the tower columns
  store top to bottom. Grids and the flat-byte fixture are generated from the disassembly by
  `tools/asm_parser/parse_tilemaps.py`, which matches each screen by its label shape, resolves mixed
  string/byte rows, and cross-checks the loader widths, the sentinel, and a 4110-byte corpus total.
  Test baseline 54 → 61; parser suite 214 → 269. See
  [`tilemaps.md`](tilemaps.md), [`../contracts/tilemaps.md`](../contracts/tilemaps.md),
  [`../engine/tilemaps.md`](../engine/tilemaps.md).

- **Tile graphics** ⬜ → ✅. The four graphics blocks — the 1bpp font and three 2bpp screens —
  ported as the extraction table `kTileGraphics` (`src/data/tile_graphics.h`) plus the in-app
  extractor (`src/assets/extract.{h,cpp}`) that decodes a player's ROM into the four greyscale
  PNGs under `assets/gfx/default/`, with the port's own PNG serialization
  (`src/assets/png_writer.{h,cpp}`). The identity gate refuses anything but the expected ROM
  (exact size + SHA1) before writing a byte; every run rewrites all four files; the fixture pins
  dimensions and content hashes, never pixels. The table generator requires the disassembly's
  dumper facts, the ROM, and the committed reference PNGs to agree pixel-for-pixel before it
  emits. Test baseline 46 → 54; parser suite 169 → 214. See
  [`tile-graphics.md`](tile-graphics.md), [`../contracts/tile-graphics.md`](../contracts/tile-graphics.md),
  [`../engine/tile-graphics.md`](../engine/tile-graphics.md).

- **ROM extraction tool** ⬜ → ✅ and **Asset acquisition** ⬜ → ✅. Delivered by the tile-graphics
  work above: the first-start flow now runs end to end — presence check, native ROM prompt, real
  extraction, normal startup — so a player's first launch needs nothing but their own ROM. The
  audio byte spans (sound driver + song data, consumed by the virtual machine) are extracted by
  the same module once the audio backend fixes their output path; they ride the audio features.
  See [`asset-acquisition.md`](asset-acquisition.md).

- **Playing-field wipe patterns** ⬜ → ✅. The 18 × 10 field geometry and the wipe schedule that
  redraws the field one row per frame, ported as four header-only constants and the counter→row
  closed form `playingFieldRowForWipeCounter` (`src/data/playing_field.h`) — the schedule is a
  mapping, not stored patterns, and the original's addresses stay off the port surface. Constants
  and the raw address-triple fixture are generated from the disassembly by
  `tools/asm_parser/parse_playing_field.py`. Test baseline 42 → 46; parser suite 140 → 169. See
  [`playing-field.md`](playing-field.md), [`../contracts/playing-field.md`](../contracts/playing-field.md),
  [`../engine/playing-field.md`](../engine/playing-field.md).

## 2026-08-06

- **Scoring tables** ⬜ → ✅. The line-clear award table (`kLineClearScores`, 40/100/300/1200 with
  the minted `LineClearKind`), the bonus-ending tiers (`kBonusEndings`, rockets at 100k/150k/200k),
  the transcribed constants (`kLevelCap` 20, `kTypeBLineGoal` 25, `kSoftDropPointsPerRow` 1,
  `kScoreSaturation` 999 999), and the four pure functions — `lineClearAward`, `softDropAward`
  (the original's minus-one quirk kept), `rocketSpriteForScore`, `shouldLevelUp` (the 1000-line
  cutoff kept) — ported to `src/data/scoring.{h,cpp}`. Every score decodes from the ROM's BCD wire
  format to plain decimal; tables and the raw-byte fixture are generated from the disassembly by
  `tools/asm_parser/parse_scoring.py`, which cross-checks the base scores across their three
  independent sites. Test baseline 35 → 42; parser suite 104 → 140. See
  [`scoring.md`](scoring.md), [`../contracts/scoring.md`](../contracts/scoring.md),
  [`../engine/scoring.md`](../engine/scoring.md). (The Systems-layer scoring flow doc, when it
  comes, is `scoring-system.md`.)

- **Gravity / frames-per-drop table** ⬜ → ✅. The 21-entry per-level drop-interval table
  (`kFramesPerDrop`, levels 0–20) and the lookup that reads it (`framesPerDrop(level, heartMode)`)
  ported to `src/data/gravity.{h,cpp}` — the first data unit with a gameplay-math consumer rather
  than a rendering one. The lookup mirrors the original exactly: heart mode shifts the index up ten
  levels and caps at 20, the normal path applies no cap, and out-of-range levels assert rather than
  invent a result. Rows and the raw-byte fixture are generated from the disassembly by
  `tools/asm_parser/parse_gravity.py`. Test baseline 31 → 35; parser suite 80 → 104. See
  [`gravity.md`](gravity.md), [`../contracts/gravity.md`](../contracts/gravity.md),
  [`../engine/gravity.md`](../engine/gravity.md).

## 2026-08-05

- **Character map** stays ✅ — the tile is now a named glyph. `CharTile`
  (`enum class : uint8_t`, 47 named glyphs) is generated from the same
  `charmap.asm` source; `CharmapEntry`, the exact lookup, and the encoder all
  carry it, so consumers read `CharTile::LETTER_A` instead of `0x0A`. The test
  fixture keeps raw bytes so the sweep pins every enumerator's value. Test
  baseline 30 → 31. See [`charmap.md`](charmap.md),
  [`../contracts/charmap.md`](../contracts/charmap.md),
  [`../engine/charmap.md`](../engine/charmap.md).

- **Sprite layout grids (+ PieceKind)** ⬜ → ✅. The five shared (y, x) offset grids the sprite
  renderer walks (`kSpriteGrid4x4` / `1x8` / `7x2` / `8x4Notched` / `3x3`, 150 bytes) ported as
  header-only `constexpr std::array<SpriteGridOffset, N>` in `src/data/sprite_grids.h`, generated
  with their test fixture from the disassembly by `tools/asm_parser/parse_sprite_grids.py`. The same
  work resolved the deferred `PieceKind` enum (`L, J, I, O, S, Z, T`) in `include/kirpich/piece_kind.h`
  and retyped `Piece::kind()` to return it. The checklist's "piece rotation matrices" name was a
  misnomer — these are shared sprite geometry, not per-piece rotation data. Test baseline 24 → 30. See
  [`sprite-grids.md`](sprite-grids.md), [`../contracts/sprite-grids.md`](../contracts/sprite-grids.md),
  [`../engine/sprite-grids.md`](../engine/sprite-grids.md).

- **Character map** ⬜ → ✅. The 47-entry `charmap.asm` sequence→tile table ported as a
  `CharmapEntry` table with exact-sequence lookup (`getCharmapTile`) and an RGBDS greedy-longest-match
  text encoder (`encodeCharmapText`) — the `.”` ligature encodes to a single tile, and the digits map
  to their own tile indices as a guarantee the score renderer can rely on. The table and its test
  fixture are generated from the disassembly by `tools/asm_parser/parse_charmap.py` (non-ASCII bytes
  emitted as `\xHH` escapes so it compiles identically everywhere), and the shared parser helpers
  moved to `tools/asm_parser/common.py` with the core-enums parser's output byte-for-byte unchanged.
  Test baseline 17 → 24. See [`charmap.md`](charmap.md), [`../contracts/charmap.md`](../contracts/charmap.md),
  [`../engine/charmap.md`](../engine/charmap.md).

## 2026-08-04

- **Core enums** ⬜ → ✅. The seven core type surfaces (`GameState`, `GameType`, `MusicType`,
  `SerialRole`, `SerialClockMode`, `SerialState`, `Piece`) ported as header-only types in
  `include/kirpich/`; the serial constants and the value fixture are generated from the disassembly
  by `tools/asm_parser/parse_core_enums.py`, the rest hand-written and drift-checked against it.
  Test baseline 9 → 17. See [`core-enums.md`](core-enums.md), [`../contracts/core-enums.md`](../contracts/core-enums.md),
  [`../engine/core-enums.md`](../engine/core-enums.md).
- **Build system** ⬜ → ✅. Feature document written; the CMake project, dependency configuration,
  and target graph build clean and pass the test suite on all five targets. See
  [`build-system.md`](build-system.md).
- **Distributable build** documented; stays ⬜. The shipping gate (the empty-asset clean check) and
  the development/ship asset-root switch are in place; the packaging target and the lean link
  configuration are designed and not yet built. See [`distributable-build.md`](distributable-build.md).

## 2026-08-03

- **Retro++ engine adoption** (new) ⬜ → ✅. Engine consumed as a submodule at `d4a6091` via
  `add_subdirectory(engine)` + `retropp::engine`; build and smoke suite green against it. See
  [`engine-adoption.md`](engine-adoption.md) and `../DESIGN.md` §12.
- **Platform abstraction facade** ⬜ → ❌. Superseded by engine adoption; the port-local SDL3
  facade was deleted, since the engine provides platform, windowing, and rendering and the port
  may not declare its own SDL3.
- **Logging** ⬜ → ✅. Resolved as direct spdlog use — the engine exposes no logging surface and a
  wrapper adds nothing at this size. No separate deliverable.
- **CPU virtualization** ⬜ → 🟫. Dropped as port-side work: the engine owns the VM host and its
  preset routines. See [`cpu-fidelity.md`](cpu-fidelity.md).
- **Tick scheduling** ⬜ → 🟫. Dropped as a separate feature; the engine run loop is the scheduler.

## 2026-06-10

- **Test harness** ⬜ → ✅. GoogleTest wired into CTest via `gtest_discover_tests()`;
  `tests/smoke_test.cpp` adds three cases (arithmetic, string comparison, C++20 designated
  initializers). First non-zero test baseline: 3 passing.

## 2026-05-15

- **Whole-project feature registry seeded.** Data, state, systems, rendering, and integration
  features registered as pending in `../FEATURES.md`.
- **Continuous integration scope expanded** ⬜ → ⬜. Was three targets (macOS, Linux, Windows); now
  five (adds Linux ARM64 and Windows ARM64). See [`ci.md`](ci.md).
- **ROM extraction tool scope reduced** ⬜ → ⬜. Folded into
  [`asset-acquisition.md`](asset-acquisition.md) rather than carrying its own document.
- **CPU virtualization** registered as pending. Prerequisite for randomization and the chiptune
  audio backend.
- **Chiptune audio backend** registered as pending. Replaces the originally-planned native audio
  driver and music engine. Chiptune only — no audio-file replacement backend.
- **Anti-channel-stealing** registered as pending. Ships as a user-toggleable option, off by
  default.
- **Asset acquisition scope reduced** ⬜ → ⬜. Single canonical asset path; no swappable packs, no
  manifest, no fallback chain.
- **Display options scope amended.** All four now explicitly require the SDL_GPU backend; the
  simpler renderer path is insufficient. No status change.
- **Build system** implementation delivered — CMake project and dependency configuration land; the
  binary builds clean on macOS and exits 0. Status stays pending until its feature document is
  written.

## 2026-05-14

- Initial registry seeded — infrastructure features registered as pending.
- All four display options registered as pending; design locked in `../DESIGN.md` §7.
