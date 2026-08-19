# Assets

Kirpich ships with no graphics and no sound. Both are derived from the Game Boy Tetris ROM,
so they are never committed and never distributed — the player supplies them from a copy of
the game they own, and the game reads what it needs out of it on first launch.

This page covers the code that makes that work: the manifest of what is required, the check
that runs at startup, the flow that acquires the files when they are absent, and the gate
that keeps them out of a packaged build.

## The model

There are two canonical locations, and two routes that populate them:

| Location | Holds |
|---|---|
| `assets/gfx/default/` | the four graphics PNGs |
| `assets/audio/default/` | `sound_driver.bin`, the game's sound driver image |

- **Development.** `scripts/setup-dev-assets.sh` (or `.ps1` on Windows) copies what it can
  out of the sibling disassembly checkout.
- **Player.** The first-start flow asks for a ROM and extracts everything from it.

Both routes write the same files under the same logical paths, and both are read by the same
code — there is no development branch in the load path, so a developer's daily run exercises
exactly what a player's install does.

What differs is the root those logical paths hang off, and only that: the development route
writes into the project tree, and a player's extraction goes to their per-user data directory.
[The asset root](#the-asset-root) is where that is decided.

The two directories under `assets/` in the repository serve the development route. Each is
committed as an empty placeholder (`.gitkeep`) and its contents are gitignored; a player's install
never has them, because a player's files are not in the program's directory at all.

## Asset paths are literals at their use sites — there is no path constant anywhere

This is the engine's rule, and it covers **every** asset family the engine ingests —
atlases, map PNGs, palette images, audio, VM routine `.asm` files — not only graphics. The
engine's build scan reads path literals (and `AssetPolicy::…` tokens) textually out of the
source at each ingest call to decide what to bake into the binary versus copy beside it. A
path stored in a named binding — a `constexpr` constant, a `LiteralPath` variable, a
`string_view`, a table — is invisible to that scan. So a path is written out as a string
literal at every point of use, and nowhere else. The engine guide is the authority:
`engine/docs/guide/assets-and-embedding.md`.

Concretely, in this codebase:

- Every future load site spells its own path inline: `loadAtlas("assets/gfx/default/font.png", …)`.
- The presence check (`src/assets/presence.cpp`) writes each required path as a literal at
  its own check call. There is no manifest array to import.
- A path known only at runtime — the player's ROM from the file picker — never appears at an
  engine load call. The extractor opens and reads that file itself.

**To add a required file:** add a `checkOne(result, "<logical path>")` line in
`checkRequired()`, then add the same literal to the expected-order assertion in
`tests/test_asset_presence.cpp` — that test is the drift alarm between the check and the
suite, and it stays red until both agree. If the file is ROM-derived, also confirm its
directory is one the distributable guard walks (see
[Keeping ROM-derived bytes out of a build](#keeping-rom-derived-bytes-out-of-a-build)).

## The presence check

```cpp
namespace kirpich::assets {

struct PresenceResult {
    std::vector<std::string> missing;   // logical paths, in manifest order
    bool complete() const noexcept;     // true when nothing is missing
};

PresenceResult checkRequired();
std::string    missingAssetsMessage(const PresenceResult& result);

}
```

`checkRequired()` tests each required path for existence against the current asset root and
returns whatever is absent, in reporting order. It **does not open, decode, or validate**
anything — a zero-byte file counts as present. It touches no renderer and needs no window,
which is what lets it run before anything is constructed. Its resolution is a plain join
against `retropp::assetRoot()` — the engine's public runtime base, which is the prescribed
route for a name that is not sitting at an engine load call.

`missingAssetsMessage()` renders the player-facing text: what is missing, and that Kirpich
is about to ask for the ROM. It never tells the player to go and run a tool.

## The asset root

Logical paths resolve against the engine's asset root, `retropp::assetRoot()`. Kirpich sets the
root once, in `main()`'s `configureAssetRoot()`, and it resolves to one of two places.

**A player's assets live in the per-user data directory** — `retropp::UserFiles{}.root()`, which
is the same directory the save file goes in: `~/Library/Application Support/<org>/<app>` on
macOS, `%APPDATA%\<org>\<app>` on Windows, `$XDG_DATA_HOME/<org>/<app>` on Linux. They are the
player's own files, extracted from their own cartridge, and they belong with their save rather
than with the program. It is the same directory wherever the binary itself sits, so a copy in a
downloads folder and a copy in an applications folder read the same extracted assets, and a
program installed somewhere read-only can still extract.

**A development build reads its project tree instead**, so a developer who has run
`scripts/setup-dev-assets` exercises the shipped load path against the files in their checkout.
`KIRPICH_PROJECT_ROOT` is defined by the build when `KIRPICH_DEV_ASSET_ROOT` is on (the default —
see [build.md](build.md)).

That tree only applies to a binary **still inside it**:

```cpp
if (const auto devRoot = kirpich::assets::developmentAssetRoot(here, root)) {
    retropp::setAssetRoot(*devRoot);
    return;
}
```

`developmentAssetRoot` (`src/assets/asset_root.h`) answers the project root when the executable's
directory is inside it, and nothing otherwise — compared per path component, so a sibling
directory whose name merely begins with the project's does not count. A binary copied elsewhere
falls through to the per-user directory like any other. Without that check a development build
carried its build machine's tree wherever it went, reading assets a player would never see and
extracting into a directory unrelated to where it was running.

If the per-user directory cannot be resolved — an identity the engine never published, or a
platform that cannot answer — `configureAssetRoot` logs it and leaves the engine's own default in
place, which is the executable's directory. The run continues; the files just land somewhere less
appropriate.

Call `setAssetRoot` from `main()` and nowhere else — not from library code, and never at
namespace scope, where the ordering against the engine's own startup is not defined. The
engine's loaders apply the root themselves; the only port code that joins against
`assetRoot()` by hand is the presence check and the extractor, whose names are not at an engine
load call.

## The first-start flow

`src/assets/first_start.h`:

```cpp
namespace kirpich::assets {

std::optional<std::filesystem::path> promptForRom();

bool ensureAssetsPresent(const std::function<void(const std::string&)>& report);

}
```

The extraction call the sequence makes — `extractFromRom` and its `ExtractionResult` — lives in
`src/assets/extract.h` and has its own page, [tile-graphics.md](tile-graphics.md).

`ensureAssetsPresent` is the whole sequence, and `main()` calls it before constructing
anything:

```cpp
const bool ready = kirpich::assets::ensureAssetsPresent(
    [](const std::string& text) { spdlog::warn("{}", text); });
if (!ready) {
    return EXIT_FAILURE;
}
```

It checks; returns `true` immediately if everything is there; otherwise reports what is
missing, prompts for a ROM, extracts, and **re-runs the check against the disk** rather than
trusting the extractor's return value. It returns `false` if the player cancels, if
extraction fails, or if the files still are not there afterwards.

Everything the caller should show the player arrives through `report`, one call per message.
Today `main()` routes that to the log; once there is a window it can go on screen instead,
with no change here.

### The dialog

`promptForRom()` shows the platform's native file picker via SDL and blocks until the player
chooses or cancels, returning `std::nullopt` on cancel or on failure to open a dialog. It
distinguishes the three outcomes internally — chosen, cancelled, failed — because SDL reports
a failure and a cancellation through the same callback, and telling a player whose dialog
broke that they declined to pick a file would be worse than saying nothing.

The picker requires SDL's dialog subsystem, which the engine leaves off by default. Kirpich
opts in from its own `CMakeLists.txt` before `add_subdirectory(engine)`:

```cmake
set(SDL_DIALOG ON CACHE BOOL "" FORCE)
```

Without that line SDL links a dummy backend and the call fails with *"SDL not built with
dialog support"* — so if the picker ever stops appearing, check this option first.

Three things about it constrain how it can be used:

- **Main thread only.** SDL requires it, and the callback may arrive on a different thread
  than the one that opened the dialog.
- **It initializes SDL video and leaves it up.** A dialog is a platform window, so video has
  to be running; SDL refcounts the request, so asking for it here is safe either way. It is
  deliberately not torn down afterwards — the panel's completion handler is still unwinding
  when the callback returns, and quitting the subsystem underneath it hangs the process on
  macOS. Video belongs to the engine from here on regardless. Kirpich has no window of its
  own this early, so the dialog is parentless, which every platform Kirpich runs on allows.
- **It pumps events while it waits.** This is required rather than polite: the portal-based
  dialogs on Linux run over DBus and never complete without it.

### Extraction

`extractFromRom` (`src/assets/extract.h`) identifies the ROM — exact size and SHA1, refusing
anything else before a byte is written — then produces every required file. It prepares all of
them in memory first, so a failure partway cannot leave a half-populated install, and every run
rewrites every file. Its messages are player-facing in both directions: what was written and
where, or why the file was refused and that nothing was written.

Two kinds of output come out of one pass:

- **The four graphics.** Each block is decoded and serialized as a greyscale PNG. The extraction
  table, the decode, the PNG serialization, and how to regenerate the table are on
  [tile-graphics.md](tile-graphics.md); the behavioral specification both population routes
  share is [`../contracts/tile-graphics.md`](../contracts/tile-graphics.md).
- **The sound driver image.** `soundDriverImage()` returns a view of the ROM span the driver
  occupies, and those bytes are copied out unchanged — the machine that runs the driver wants
  exactly what the cartridge held, so there is nothing to decode.

```cpp
namespace kirpich::assets {

inline constexpr std::size_t kSoundDriverImageBase;   // the audio section's base
inline constexpr std::size_t kSoundDriverImageEnd;    // the end of the ROM
inline constexpr std::size_t kSoundDriverImageSize;   // 7040 bytes
inline constexpr std::size_t kSoundDriverTickEntry;   // run once per frame
inline constexpr std::size_t kSoundDriverInitEntry;   // run once at start

std::span<const std::uint8_t> soundDriverImage(std::span<const std::uint8_t> rom);

}
```

The image is a single span rather than one file per song, because the driver reaches its own
data by absolute address: its music pointer table holds raw addresses into the sequence region,
and its effect tables hold raw addresses of driver routines. Separating the data from the code
that reads it would only work if every piece were placed back at the address it came from, so
the span carries all of it. The unused padding between the driver's last data and its two entry
trampolines is inert — the driver never executes or reads it.

## Loading

There is no port-side loading wrapper, and adding one would be a mistake. The engine already
models this exact case: its `LoadFromPath` policy exists for content that may never be baked
into a binary, image atlases default to that policy, and the asset root is a runtime base
that a development build and an installed game point at different places. Kirpich sets the
root and addresses assets by logical path; there is nothing left for a wrapper to do.

`src/assets/` holds the presence check and the acquisition flow. Those are a different job
from loading, and it stays that way.

## Keeping ROM-derived bytes out of a build

`scripts/check-distributable-clean.sh` walks every directory that holds ROM-derived content —
`assets/gfx/default/` and `assets/audio/default/` — and exits non-zero if either holds anything
but `.gitkeep`. A new ROM-derived directory is added to the loop at the top of the script:

```
scripts/check-distributable-clean.sh /path/to/staged/package
```

With no argument it checks this working tree — which is *expected to fail* for a developer
who has run the setup script. That is the check demonstrating it can tell the two states
apart, not a problem to fix.

The directories it walks are the development route's. A player's extraction goes to their own
data directory and never near a package, so what this guards against is a package staged out of
a tree that has been populated for development — which is the realistic accident, and the reason
the check runs against the staged output rather than the source.

## Testing without copyrighted bytes

The real graphics are gitignored and absent from CI, so no test may depend on them. Tests
use `tests/fixtures/tiny_probe.png` instead — an 8×8 2-bit greyscale PNG authored for this
repository and derived from nothing, where pixel (x, y) has sample value `(x + y) % 4`. It
is the same encoding family as the real assets, so it exercises the same decode path. The
presence check is existence-only, so the same probe stands in for any required file
regardless of its real format — the sound driver image included.

`tests/test_asset_presence.cpp` points the asset root at a scratch directory, populates it
with whatever subset a case needs, and asserts exactly which paths come back missing. The
root is restored afterwards; it is process-global state, so a test that sets it must put it
back.

It also checks that every required path is one the file store will accept and resolves *inside*
the store's own directory. The store refuses a name that could escape it, and allows separators so
a name can express a tree — which is what these paths need, and what the save store deliberately
refuses. That property is the extractor's dependency on the store; if it ever tightened, extraction
would fail on a player's first launch and nothing else here would see it.

`tests/test_driver_image.cpp` covers the driver span itself. It reads the real ROM — resolved
from the CI provisioning path, then the development sibling — and fails loudly when neither is
present, because a missing ROM is a provisioning failure rather than a reason to skip. It
extracts into a scratch root, never the real asset tree.

This is why there is no skip-if-absent test anywhere in this area: a skipped test reports
green while verifying nothing.
