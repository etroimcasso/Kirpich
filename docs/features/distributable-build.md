# Distributable Build

**Date:** 2026-08-04
**Status:** Partially implemented — the binary is lean and the shipping gate checks it; the
packaging target does not exist yet

How a shippable Kirpich artifact is produced: a lean binary, empty asset directories, and no
copyrighted or ROM-derived byte anywhere in it. This document records the design; the build itself
is in [`build-system.md`](build-system.md), and the asset side is in
[`asset-acquisition.md`](asset-acquisition.md).

## Concept

A distributable is the game binary plus its tooling and documentation — nothing derived from the
ROM. Two properties define it and both are non-negotiable:

- **No copyrighted content.** Graphics and the VM's byte spans come from the player's own ROM at
  runtime; none of them ship. The asset directories ship as empty structure.
- **Lean.** The binary is optimized and stripped of what nothing needs at runtime. An unoptimized
  or unstripped shipped binary is a defect, not a neutral default.

Both properties are enforced by things that exist: the binary is built lean, and the gate checks
both the asset directories and the binary itself. What is missing is the packaging step that
assembles the pieces into a platform artifact.

## Design decisions

### No copyrighted content ships — enforced, not trusted

The asset directories carry only their `.gitkeep` placeholder in a shipped build. A packaging gate
refuses any artifact that carries more:

`scripts/check-distributable-clean.sh` exits non-zero if either `assets/gfx/default/` or
`assets/audio/default/` holds anything but `.gitkeep`. It runs against a staging tree, and takes the
binary to check as an optional second argument:

```sh
scripts/check-distributable-clean.sh /path/to/staged/package
scripts/check-distributable-clean.sh /path/to/staged/package /path/to/Kirpich.app
```

With no argument it checks the working tree, which is *expected to fail* for a developer who has run
the setup script — that failure is the gate proving it can tell a populated tree from a clean one,
not a problem to fix. `.gitignore` bans ROM extensions tree-wide as a second line of defence, so a
ROM cannot be committed even by accident.

The gate is a script rather than a build step today because there is no packaging target for it to
sit inside yet. When one lands, the gate runs as part of it, and in CI against the packaged output.

### The gate checks the binary, and does not take the build's word for it

A build configured with `KIRPICH_DEV_ASSET_ROOT` on bakes its own source tree's path into the
executable, and a binary carrying that path looks for assets in a directory that exists on one
machine in the world. Turning the option off is what prevents it; the gate is what confirms it
happened.

It confirms it by reading the paths out of the binary and refusing any that lead to a source tree —
recognised by holding both `CMakeLists.txt` and `src/main.cpp`. That catches a binary built from any
checkout, not just the one being packaged, and it reports the offending path rather than a verdict.
A binary the gate cannot find or cannot read is a failure too, not a step quietly skipped: a package
whose binary was never checked has not been checked.

This is the one property neither the compiler nor the test suite can observe. A binary that resolves
its assets against the machine that built it passes every test, on that machine.

### The asset root flips to the player's own data directory

A development build resolves the asset root to the project tree so a developer exercises the shipped
load path against real files in their checkout. A distributable build turns `KIRPICH_DEV_ASSET_ROOT`
**off** at configure time: `KIRPICH_PROJECT_ROOT` is then undefined, and every build resolves the
per-user data directory — the same one the save file goes in, and where the runtime extractor writes
on a player's machine.

The option is the switch between the two postures, but it is not the only thing that decides: the
project tree applies only to a binary still inside it, so even a development build that has been
copied elsewhere resolves the per-user directory. There is no development branch in the load path
itself. Full mechanics in [`asset-acquisition.md`](asset-acquisition.md).

### The binary is lean in every build that is not a Debug build

On top of the `Release` default that every build already gets
([`build-system.md`](build-system.md)), the linker drops what nothing reaches and the symbol table
comes off:

- **Apple (`ld`):** `-Wl,-dead_strip` at link, then `strip -x`, which keeps the global symbols and
  drops the local ones.
- **GNU / LLVM (`ld`):** `-ffunction-sections -fdata-sections` at compile, `-Wl,--gc-sections` at
  link, then a symbol strip.
- **MSVC:** nothing added. Its release link already drops unreferenced code (`/OPT:REF /OPT:ICF`)
  and puts symbols in a separate `.pdb` rather than in the executable.

**This applies to every non-Debug build, not only to a packaged one.** The binary a continuous
integration job uploads is the binary a player would run, so a leanness that only switched on
inside a packaging path would leave the artifact people actually get built the fat way and never
exercised the lean way. A Debug build keeps all of it.

Measured on macOS arm64, `Release`, before and after:

| | Before | After |
|---|---|---|
| Size | 4 610 296 B | 3 733 448 B (−19%) |
| Symbols | 13 816 | 3 443 (−75%) |

The strip has to coexist with the routine bake, and does. Each baked SM83 routine's registry is
anchored by an `extern "C"` symbol the engine names undefined at link time, which makes it a root
the dead strip keeps and a global symbol `strip -x` does not touch; all three anchors are present in
the stripped binary. `nm` on the built executable piped through `grep retropp_routine_` is the
check, and `tests/test_embedded_routines.cpp` is the guard that the routines still register.

Link-time optimization and a size-oriented optimization level are worth measuring against these
numbers, and are not applied today.

### macOS ships a disk image

`scripts/make-macos-dmg.sh` builds it: the app and a symlink to `/Applications`, compressed
read-only. That is how a Mac application is distributed and it is what someone can actually open.

**Both the continuous-integration job and the release build the image with this same script**, so
what gets tested and what gets shipped differ only by the signature and the notarization ticket. The
script signs nothing — a development build has nothing to sign with, and the release signs the image
after the script has produced it.

An archive was the alternative and is worse in two ways: it arrives as a folder of files rather than
something to drag, and anything but `ditto` drops the bundle's symlinks and its executable bit on
the way through. Handing a `.app` to GitHub's upload-artifact action has the same problem for a
sharper reason — it uploads the *files under* a directory, so the bundle arrives with no wrapper at
all.

### The distributable is assembled by a packaging step, not by hand

The mechanism that stages a shippable tree — the binary, the empty asset structure, tooling, and
documentation, in the layout each platform expects — is an open decision (see below). Whatever form
it takes, it turns `KIRPICH_DEV_ASSET_ROOT` off, applies the lean link configuration, empties the
asset directories, runs the clean gate against the staged result, and produces the per-platform
bundle.

## Implementation details

**Delivered:**

- `scripts/check-distributable-clean.sh` — the packaging gate; POSIX shell, exits non-zero on any
  ROM-derived content in the asset directories and on a binary carrying a development asset root.
- The lean link and strip configuration (in `src/CMakeLists.txt`), applied to every non-Debug build.
- `scripts/make-macos-dmg.sh` — the disk image, built identically by CI and by the release.
- `.github/workflows/release.yml` — a version tag builds, signs, notarizes, staples and publishes
  the macOS distributable on the trusted runner.
- `KIRPICH_DEV_ASSET_ROOT` (in `src/CMakeLists.txt`) — off for a distributable; the asset root is
  then the player's own per-user data directory, beside their save.
- `.gitignore` ROM-extension bans — the backstop against committing ROM-derived bytes.

**Not yet built:**

- Linux and Windows distributables. The Linux binary is already self-contained and opens no terminal
  when launched from a desktop, so what it wants is a `.desktop` entry and an icon rather than a
  dependency bundle.
- An icon, on any platform.

## Open questions

- **The packaging mechanism.** CMake `install` + CPack, or a per-platform staging script. CPack
  gives native bundle formats for free; a script gives exact control over the tree. Decided when the
  target is built.
- **Per-platform bundle format.** A macOS `.app`, a Linux tarball or AppImage, a Windows folder or
  installer — each platform's convention, settled with the packaging mechanism.
- **Whether the packaging step configures its own build.** The lean configuration applies to every
  non-Debug build, but `KIRPICH_DEV_ASSET_ROOT=OFF` is a configure-time choice a packaging step has
  to make deliberately. Whether it configures a build of its own or packages an existing one is
  settled with the packaging mechanism; either way the gate checks the result.
