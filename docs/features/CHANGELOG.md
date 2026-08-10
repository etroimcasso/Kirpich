# Features Changelog

Chronological log of feature status transitions, newest first. Entries are not edited after they
are written. `../FEATURES.md` holds current status; this file holds history.

**Format:** `<feature> <old status> → <new status>` with a one-line reason. Glyphs match
`../FEATURES.md`: ⬜ pending · 🟡 in progress · ✅ delivered · 🟫 dropped · ❌ rejected.

---

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
