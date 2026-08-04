# Assets

Kirpich ships with no graphics. They are derived from the Game Boy Tetris ROM, so they are
never committed and never distributed — the player supplies them from a copy of the game
they own, and the game reads what it needs out of it on first launch.

This page covers the code that makes that work: the manifest of what is required, the check
that runs at startup, the flow that acquires the files when they are absent, and the gate
that keeps them out of a packaged build.

## The model

There is one canonical location for graphics — `assets/gfx/default/` — and two routes that
populate it:

- **Development.** `scripts/setup-dev-assets.sh` (or `.ps1` on Windows) copies the four
  PNGs out of the sibling disassembly checkout.
- **Player.** The first-start flow asks for a ROM and extracts them.

Both routes write the same files to the same place, so there is no development branch in
the load path — a developer's daily run exercises exactly the code a player's install does.

The directory is committed as an empty placeholder (`.gitkeep`) and its contents are
gitignored.

## The manifest

`src/assets/presence.h` names every graphic the game requires, as a logical path relative
to the project root:

```cpp
inline constexpr retropp::LiteralPath kConfigAndGameplay{"assets/gfx/default/configandgameplay.png"};
inline constexpr retropp::LiteralPath kFont{"assets/gfx/default/font.png"};
inline constexpr retropp::LiteralPath kCopyrightAndTitleScreen{"assets/gfx/default/copyrightandtitlescreen.png"};
inline constexpr retropp::LiteralPath kMultiplayerAndBuran{"assets/gfx/default/multiplayerandburan.png"};

inline constexpr retropp::LiteralPath kRequired[]{
    kConfigAndGameplay, kFont, kCopyrightAndTitleScreen, kMultiplayerAndBuran,
};
```

`retropp::LiteralPath` only constructs from a string literal — a `const char*`,
`std::string`, or computed path will not compile. That is deliberate on the engine's side:
these paths are readable by a build-time scan, which a runtime-assembled string would be
invisible to. The practical consequence for you is that **an asset path is written out in
full at its declaration**, never concatenated.

**To add a required graphic:** declare it alongside the others and add it to `kRequired`.
Everything downstream — the presence check, the missing-asset message, the tests that sweep
the manifest — reads that array, so nothing else needs editing.

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

`checkRequired()` tests each manifest entry for existence against the current asset root and
returns the logical paths of whatever is absent, in manifest order. It **does not open,
decode, or validate** anything — a zero-byte file counts as present. It touches no renderer
and needs no window, which is what lets it run before anything is constructed.

`missingAssetsMessage()` renders the player-facing text: what is missing, and that Kirpich
is about to ask for the ROM. It never tells the player to go and run a tool.

## The asset root

Logical paths resolve against the engine's asset root, `retropp::assetRoot()`, joined by
`retropp::assetPath(logical)`. Kirpich sets the root once, in `main()`:

```cpp
#ifdef KIRPICH_PROJECT_ROOT
    retropp::setAssetRoot(std::filesystem::path{KIRPICH_PROJECT_ROOT});
#endif
```

`KIRPICH_PROJECT_ROOT` is defined by the build when `KIRPICH_DEV_ASSET_ROOT` is on (the
default — see [build.md](build.md)), so a development build reads the files the setup script
wrote into the source tree. With the option off, the definition is absent, `setAssetRoot` is
never called, and the engine's own default applies: the executable's directory, which is
where the extractor writes on a player's machine.

Two rules worth holding to. Call `setAssetRoot` from `main()` and nowhere else — not from
library code, and never at namespace scope, where the ordering against the engine's own
startup is not defined. And read assets through `assetPath()` rather than building a base
path by hand; that join is the one place the root is applied.

## The first-start flow

`src/assets/first_start.h`:

```cpp
namespace kirpich::assets {

std::optional<std::filesystem::path> promptForRom();

struct ExtractionResult {
    bool        succeeded = false;
    std::string message;        // player-facing, whether it worked or not
};

ExtractionResult extractFromRom(const std::filesystem::path& romPath);

bool ensureAssetsPresent(const std::function<void(const std::string&)>& report);

}
```

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

> **The picker cannot open in the current build.** The engine compiles SDL with its dialog
> subsystem disabled (`SDL_DIALOG OFF ... FORCE`), so SDL links a dummy backend and the call
> fails with *"SDL not built with dialog support"*. The flow handles it correctly — it logs
> the reason and exits rather than hanging or crashing — but first-start ROM selection cannot
> complete until the engine allows a consumer to opt the subsystem in. Populate assets with
> the setup script or by hand meanwhile.

Three things about it constrain how it can be used:

- **Main thread only.** SDL requires it, and the callback may arrive on a different thread
  than the one that opened the dialog.
- **It initializes SDL video if that is not already up**, because a dialog is a platform
  window, and it shuts the subsystem down again if it was the one that started it. Kirpich
  has no window of its own this early, so the dialog is parentless — allowed everywhere
  Kirpich runs.
- **It pumps events while it waits.** This is required rather than polite: the portal-based
  dialogs on Linux run over DBus and never complete without it.

### Extraction

`extractFromRom` is **not implemented yet.** It writes nothing and returns `succeeded =
false` with a message saying so. That is a deliberate choice over a stub that reports
success: a player must never see a confirmation followed by a failure to load.

The design it will be built to — ROM identification by SHA1, the four tile-data offsets, the
1bpp-versus-2bpp decode, PNG output — is pinned in
[`../../tools/rom_extractor/README.md`](../../tools/rom_extractor/README.md). Until it
lands, place the files by hand at the manifest's paths, or run the setup script if you have
the disassembly checked out beside this repository.

## Loading

There is no port-side loading wrapper, and adding one would be a mistake. The engine already
models this exact case: its `LoadFromPath` policy exists for content that may never be baked
into a binary, image atlases default to that policy, and the asset root is a runtime base
that a development build and an installed game point at different places. Kirpich sets the
root and addresses assets by logical path; there is nothing left for a wrapper to do.

`src/assets/` holds the presence check and the acquisition flow. Those are a different job
from loading, and it stays that way.

## Keeping ROM-derived bytes out of a build

`scripts/check-distributable-clean.sh` exits non-zero if `assets/gfx/default/` holds
anything but `.gitkeep`:

```
scripts/check-distributable-clean.sh /path/to/staged/package
```

With no argument it checks this working tree — which is *expected to fail* for a developer
who has run the setup script. That is the check demonstrating it can tell the two states
apart, not a problem to fix.

## Testing without copyrighted bytes

The real graphics are gitignored and absent from CI, so no test may depend on them. Tests
use `tests/fixtures/tiny_probe.png` instead — an 8×8 2-bit greyscale PNG authored for this
repository and derived from nothing, where pixel (x, y) has sample value `(x + y) % 4`. It
is the same encoding family as the real assets, so it exercises the same decode path.

`tests/test_asset_presence.cpp` points the asset root at a scratch directory, populates it
with whatever subset a case needs, and asserts exactly which paths come back missing. The
root is restored afterwards; it is process-global state, so a test that sets it must put it
back.

This is why there is no skip-if-absent test anywhere in this area: a skipped test reports
green while verifying nothing.
