# Engine documentation

Guide for working with Kirpich's C++ surfaces — building it, running it, changing how it
behaves, and building on top of what is already there. Each area has its own page; the
index is below.

These pages describe the surface as it exists: what a type holds, what a function returns,
what it throws, where the backing data lives, and what to edit to change behavior. They are
written for someone modifying or extending Kirpich, not for someone checking it against the
original game — behavioral specifications reverse-derived from the Game Boy version live
separately in [`../contracts/`](../contracts/), and the design rationale behind a given
feature lives in [`../features/`](../features/).

Kirpich is a native reimplementation, not an emulator. It runs as ordinary C++ on top of
the [Retro++](https://github.com/etroimcasso/GBCPP-Engine) engine, which supplies the
platform layer, run loop, renderer, audio, and the virtual machine that hosts the handful
of routines that need one. Where a page says "the engine", it means Retro++; where it says
"the port" or "Kirpich", it means the code in this repository.

## Pages

| Page | Covers |
|---|---|
| [assets.md](assets.md) | How the game gets its graphics: the required-asset manifest, the presence check, the first-start ROM selection flow, the asset root, and the packaging gate that keeps ROM-derived bytes out of a distributable. |
| [build.md](build.md) | Targets and how they fit together, the engine submodule, build options, and how to build, run, and test. |
| [core-enums.md](core-enums.md) | The fundamental value types — game state, game type, music type, the serial types, and the piece byte — where they live, which are generated from the disassembly, and how to regenerate and change them. |
| [charmap.md](charmap.md) | The character map — how text becomes named glyphs: the `CharTile` enum, the entry type, the exact-sequence lookup and greedy-longest-match encoder, where the table lives, and how to regenerate it. |
| [sprites.md](sprites.md) | The composed sprites — every multi-tile sprite resolved into a part list, the `SpriteId` identity space, the `PieceKind` enum, where they live, and how to regenerate them. |
| [sprite-scenes.md](sprite-scenes.md) | The sprite objects each scripted scene places on screen — the victory, defeat, dance, and launch scenes, the menu markers, and the falling-piece templates, where they live, and how to regenerate them. |
| [gravity.md](gravity.md) | How fast pieces fall — the per-level drop-interval table, the lookup and its heart-mode shift, the level bounds, where the table lives, and how to regenerate it. |
| [scoring.md](scoring.md) | What points are worth — the line-clear award table and its level multiplier, the soft-drop quirk, the rocket bonus-ending tiers, the level-up rule, where the tables live, and how to regenerate them. |
| [playing-field.md](playing-field.md) | The board's fixed extent (18 × 10) and the wipe schedule that redraws it a row per frame — the geometry constants, the counter→row mapping, where they live, and how to regenerate them. |
| [tile-graphics.md](tile-graphics.md) | The graphics themselves — the extraction table naming which ROM bytes are which asset, the in-app extractor that turns a player's ROM into the PNGs the engine loads, the PNG serialization, and how to regenerate the table. |
| [tilemaps.md](tilemaps.md) | The static screens the game draws — the 22 background tilemap grids (full screens, banners, field overlays, window messages, tower columns, and the congratulations strip), how text rows decode through the character map, where they live, and how to regenerate them. |
| [garbage-init.md](garbage-init.md) | The garbage a Type B game starts under — the fixed demo garbage table and the constants the procedural fill and its start paths use, where they live, and how to regenerate them. |
| [music.md](music.md) | The music data — the `MusicId` identifiers and the addresses that locate the song/channel/section graph, the stereo table, and the note-length tables in the sound driver's ROM image, where they live, and how to regenerate them. |
| [sfx.md](sfx.md) | The sound-effect data — the three effect-ID spaces the game triggers effects by and the register images, ramps, and driver tables the effect routines read, where they live, and how to regenerate them. |
| [demo.md](demo.md) | The attract-mode demo recordings — the two input timelines of held game actions and the shared piece list both demos replay, the action vocabulary they resolve to, where they live, and how to regenerate them. |
| [misc.md](misc.md) | The loose tables and constants — the directly-drawn sprite-object tables, the menu cursor coordinate tables, the win-screen strings and the pause label, and the demo/completed-row constants, where they live, and how to regenerate them. |
| [engine-state.md](engine-state.md) | The game's mutable global state — the score and its line-clear bookkeeping, the sprite staging buffer, and the piece ring, as one `EngineState` struct, plus the layout fixture that pins its widths, and how to use and regenerate it. |
| [game-state-machine-state.md](game-state-machine-state.md) | The state the main loop lives in — the dispatch index, the frame and drop timers, the menu selections, and the piece-pipeline counters, as one `GameFlowState` struct, plus the layout-and-census fixture that pins its widths and proves every raw-accessed byte has an owner. |
| [sprite-renderer-state.md](sprite-renderer-state.md) | The live sprite-object array — the sixteen sprite descriptors the menus, pieces, and scenes write and the renderer compiles into the object buffer, as one `SpriteRendererState` of `SpriteSlot`s (visibility, position, composed-sprite id, flip/palette flags, dancer animation), and how it is checked against the existing fixtures. |
| [serial-multiplayer-state.md](serial-multiplayer-state.md) | The state two Game Boys share over the link cable — the master/slave role and serial protocol bytes, the in-round status exchange, the received-garbage pipeline, the match win tally, and the pause save slots, as one `MultiplayerState` struct plus the `RoundOutcome` enum, and how it is checked against the existing layout+census fixture. |
| [demo-state.md](demo-state.md) | The state the attract-mode demo carries between frames — which demo is running, the recording flag, the run-length countdown, the timeline cursor, and the demo's held buttons plus the player's parked real input, as one `DemoState` struct plus the `ActiveDemo` enum, and how it is checked against the existing layout+census fixture. |
| [high-score-state.md](high-score-state.md) | The top-score surface — the two high-score tables (Type B by level/height/rank, Type A by level/rank) and the four bytes the score-entry flow uses, as one `HighScoreState` struct with a `TopScoreEntry` cell type, plus the persistence surface that saves the tables to disk and loads them back across launches. |
| [playing-field-state.md](playing-field-state.md) | The board the game plays on — the 32 × 32 tile grid the original keeps at `$C800` (the authoritative field that collision reads, locking writes, line clears scan, and garbage fills) and the 10-cell multiplayer attack staging row, as one `PlayingFieldState` struct with a `fieldCell` accessor into the visible field, and how it is checked against the existing fixtures. |
| [piece-random.md](piece-random.md) | How the game draws random pieces — the divider-fed draw core hosted on the virtual machine, the up-to-three-try rejection loop and one-stage pipeline of the native selection, where they live, and what to edit to change the fold or the rejection rule. |
| [input.md](input.md) | How the game reads input — the per-frame joypad snapshot and its held/pressed edge relation over the engine's action system, the shared key-repeat (DAS) core and its constants, the default keyboard and gamepad bindings, where they live, and what to edit to change the edge rule, the repeat timing, or the bindings. |
| [dispatcher.md](dispatcher.md) | How the game runs one frame — the state dispatch table, the frame beats (sample, dispatch, audio, soft-reset chord, timers), the game-state aggregate every handler reads and writes, and the seams for the audio tick and soft reset, where they live, and what to edit to add a state's behavior or change a frame beat. |
| [piece-system.md](piece-system.md) | How the active piece is manipulated each frame — spawn, drop by gravity or soft drop, rotate and shift with auto-repeat, collide, and lock; the shared cell geometry that finds the piece's board cells, the audio cue mailbox the routines write, where they live, and what to edit to change the drop timing, the collision rule, or the next-piece choice. |
| [line-clear.md](line-clear.md) | What happens once a piece locks — the scan for completed rows and the line/stat/garbage tally, the flash cadence, the stack compaction and its top-row quirk, and the row-by-row field wipe that spawns the next piece; the two frame beats the pipeline spans, where the functions live, and what to edit to change the scan, the tally, the flash timing, or the wipe. |
| [scoring-system.md](scoring-system.md) | How play turns into points — the Type A live line-clear award, the Type B end-of-round count-up and its cadence, the Type A level-up (wired into the field wipe), the Type B scoreboard row printer, and the scoring reset; the three frame beats they run in, where they live, and what to edit to change the award gates, the tally state machine, the level-up law, or the reset span. |
| [menu-screens.md](menu-screens.md) | The whole pre-game flow — the copyright and title screens, the config screen, the game-type and music-type selectors, and the Type A / Type B difficulty pickers; the menu action vocabulary, the shared cursor placement and blink, the attract countdown and top-score refresh seams, where they live, and what to edit to change a screen's input law, the cursor positions, or the bindings. |
| [sound-driver.md](sound-driver.md) | How the game makes sound — the game's original sound driver hosted as a resident machine, the registration that describes it (image placement, entry points, the six bytes it shares with the game), the per-frame decision that turns the cue mailbox into requests, the ordering that keeps an initialisation from silencing the sound beside it, where they live, and what to edit to change the placement, the shared bytes, or what a frame asks for. |
| [gameplay.md](gameplay.md) | The states a round passes through — the shared init for both game types and the attract demo, the twelve-step gameplay frame, the pause (shared with two-player) and its cue mailbox, the game-over chain and its rocket endings, and the Type B results re-arm; the wiring the handlers need, where they live, and what to edit to change a round's starting conditions, the frame's step order, or the pause law. |

Pages group into subdirectories once there are enough of them to warrant it — for now the
surface is small enough that a flat list is easier to scan.

## Status

Kirpich is early. What exists today is the build, the engine wiring, the asset pipeline —
including the extractor that produces the graphics from a player's ROM — the full data
layer, the full state layer, and the systems layer so far (the piece randomizer, the input
layer, the game-state dispatcher framework, the piece system, the line-clear pipeline, the
scoring pipeline, the whole pre-game flow, and the gameplay session — a solo round now runs
end to end from the title screen through play to the game-over screen). Two-player play, the
demo, the bonus-ending scenes, top-score entry, audio, and the rendering layer are not
written yet. Pages appear as
their surfaces do, so an area missing from the index above is an area that does not exist
yet rather than one that is undocumented.
