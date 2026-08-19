# Distributable Build

**Date:** 2026-08-04
**Status:** In design — the shipping gate exists; the packaging target does not yet

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

The pieces that enforce the first property are built and in use. The packaging step that assembles
an artifact, and the lean-shipping configuration layered on top of `Release`, are designed here and
not yet implemented.

## Design decisions

### No copyrighted content ships — enforced, not trusted

The asset directories carry only their `.gitkeep` placeholder in a shipped build. A packaging gate
refuses any artifact that carries more:

`scripts/check-distributable-clean.sh` exits non-zero if `assets/gfx/default/` holds anything but
`.gitkeep`. It runs against a staging tree:

```sh
scripts/check-distributable-clean.sh /path/to/staged/package
```

With no argument it checks the working tree, which is *expected to fail* for a developer who has run
the setup script — that failure is the gate proving it can tell a populated tree from a clean one,
not a problem to fix. `.gitignore` bans ROM extensions tree-wide as a second line of defence, so a
ROM cannot be committed even by accident.

The gate is a script rather than a build step today because there is no packaging target for it to
sit inside yet. When one lands, the gate runs as part of it, and in CI against the packaged output.

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

### Lean binary is the shipping target

A shipped binary is built optimized and stripped. On top of the `Release` default that every build
already gets ([`build-system.md`](build-system.md)), a distributable adds dead-code elimination and
symbol stripping so the artifact carries only what it runs:

- **Apple (`ld`):** `-Wl,-dead_strip`, then a symbol strip.
- **GNU / LLVM (`ld`):** `-ffunction-sections -fdata-sections` at compile, `-Wl,--gc-sections` at
  link, then a symbol strip.
- **MSVC:** `/OPT:REF /OPT:ICF`, which the linker applies for a release configuration.

Link-time optimization and a size-oriented optimization level are worth measuring once there is a
real binary to weigh. This configuration is pinned as the target posture and is not in the build
yet — it lands with the packaging target, and the artifact is measured before and after so the
reduction is a number, not a claim.

### The distributable is assembled by a packaging step, not by hand

The mechanism that stages a shippable tree — the binary, the empty asset structure, tooling, and
documentation, in the layout each platform expects — is an open decision (see below). Whatever form
it takes, it turns `KIRPICH_DEV_ASSET_ROOT` off, applies the lean link configuration, empties the
asset directories, runs the clean gate against the staged result, and produces the per-platform
bundle.

## Implementation details

**Delivered:**

- `scripts/check-distributable-clean.sh` — the packaging gate; POSIX shell, exits non-zero on any
  ROM-derived content in the asset directories.
- `KIRPICH_DEV_ASSET_ROOT` (in `src/CMakeLists.txt`) — off for a distributable; flips the asset root
  to the executable's directory.
- `.gitignore` ROM-extension bans — the backstop against committing ROM-derived bytes.

**Not yet built:**

- A packaging / install target (CMake `install` rules, CPack, or a staging script — undecided).
- The lean link configuration above, layered on `Release` for the shipping build.
- CI wiring: the clean gate run against the packaged artifact, and a distributable build exercised
  per platform.

## Open questions

- **The packaging mechanism.** CMake `install` + CPack, or a per-platform staging script. CPack
  gives native bundle formats for free; a script gives exact control over the tree. Decided when the
  target is built.
- **Per-platform bundle format.** A macOS `.app`, a Linux tarball or AppImage, a Windows folder or
  installer — each platform's convention, settled with the packaging mechanism.
- **The runtime ROM extractor is a shipping prerequisite.** A distributable that cannot turn a
  player's ROM into assets is not shippable. The extractor's design is pinned in
  [`asset-acquisition.md`](asset-acquisition.md); its implementation gates the first real
  distributable.
- **Measured size baseline.** Recorded here once the lean configuration lands and an artifact can be
  weighed before and after stripping.
