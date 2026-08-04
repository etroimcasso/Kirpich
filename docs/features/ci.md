# Continuous Integration

**Date:** 2026-05-15; revised 2026-08-03 for engine adoption
**Status:** In design

Automated per-commit build and test validation on five self-hosted runners: Linux x64, macOS ARM64,
Linux ARM64, Windows x64, and Windows ARM64.

## Concept

CI runs on every push to `ci/**` branches, so `main` never sees intermediate "fix the runner"
commits — a branch squashes onto `main` once all five jobs are green. The goal is a green-build gate
from the start of implementation rather than one retrofitted after the codebase has grown.

## Design decisions

### Self-hosted runners

Self-hosted is the right call here for two reasons:

1. **Persistent dependency cache.** Fetched dependencies accumulate on the runner's disk between
   runs, so the heaviest ones build once and are reused.
2. **Compiler caching.** `ccache` is only effective on persistent machines, which retain its
   directory between runs.

### Runner topology

| Target | `runs-on` labels | Compiler | Generator |
|---|---|---|---|
| Linux x64 | `[self-hosted, Linux, X64, beefserve]` | GCC ≥ 13 | Ninja |
| macOS ARM64 | `[self-hosted, macOS, ARM64, ericmacmini]` | Apple Clang | Ninja |
| Linux ARM64 | `[self-hosted, Linux, ARM64, linmac-arm64]` | GCC ≥ 13 | Ninja |
| Windows x64 | `[self-hosted, X64, Windows, Windows_X64]` | ClangCL (VS 2022) | Visual Studio 17 2022, x64 |
| Windows ARM64 | `[self-hosted, ARM64, Windows, WINMACARM64]` | ClangCL (VS 2022 + ARM64 tools) | Visual Studio 17 2022, ARM64 |

Each job is pinned by its machine's **name-label** (the last entry), not just the generic
platform/architecture set. Nothing else is registered here today, so the generic labels would be
unambiguous — but a runner added later could silently start matching, and a job that lands on the
wrong machine is a confusing failure. Pinning costs one label and removes the class.

### The engine submodule needs a credential

`engine/` is a private repository, and a job's `GITHUB_TOKEN` is scoped to this repository alone, so
the default checkout cannot descend into it. Checkout runs **without** submodules; a following step
initializes just the engine, supplying a `ENGINE_PAT` repository secret per-invocation:

```sh
git -c "url.https://x-access-token:${ENGINE_PAT}@github.com/.insteadOf=https://github.com/" \
    submodule update --init --recursive -- engine
```

The credential is passed with `-c` rather than written to the runner's git config, so it does not
persist on the machine between runs. Recursion is required — the engine builds SDL3 and SameBoy from
its own submodules.

A leftover clone from an earlier run can lack the revision a branch records, surfacing as "Unable to
find current revision." The step retries once after `git submodule deinit -f -- engine`, which costs
a full fetch only on the run that hits it.

### The ARM64 jobs run in sequence

The three ARM64 targets — macOS native plus a Linux VM and a Windows VM — share one Apple Silicon
host. That host does not have the memory to run three concurrent C++ builds, so the jobs are
serialized with `needs:`:

```yaml
build-linux-arm64:
  needs: build-macos
  if: ${{ always() }}

build-windows-arm64:
  needs: build-linux-arm64
  if: ${{ always() }}
```

macOS runs first (native, no VM overhead). When it finishes — pass *or* fail — the Linux ARM64 job
runs, then the Windows ARM64 job.

**All three ARM64 jobs are serial, and Windows ARM64 is last because it is the slowest.** Putting
the longest job anywhere but the end would park the two shorter ones behind it, so their results —
and their artifacts — would arrive later than they need to. Ordering shortest-to-longest gets every
result out as early as that job can produce it. This ordering is deliberate: a future change that
reshuffles the chain should keep Windows ARM64 at the end.

Two properties are non-negotiable:

1. **`if: ${{ always() }}` on the chained jobs.** Without it, a failure upstream would skip
   everything downstream, and those targets would go untested. With it, all five jobs always run and
   all five always upload artifacts. The workflow still fails overall if any test fails, so the gate
   is preserved — the downstream jobs simply aren't masked by an upstream failure.
2. **Five separate jobs, not a matrix.** The x64 targets run in parallel on their own host and don't
   share the ARM64 host's memory constraint, so they are independent job entries rather than rows in
   a strategy matrix.

### No test output is ever lost

Every job pipes its test step to a file — `tee` on POSIX, `Tee-Object` on Windows — and uploads it
as an artifact with `if: ${{ always() }}`, so output survives a failing run:

```bash
ctest --test-dir "$BUILD_DIR" --output-on-failure 2>&1 \
  | tee "$GITHUB_WORKSPACE/ci-output/<target>-test-results.txt"
```

**Each Unix test step sets `pipefail` explicitly.** The default shell for a `run:` step on Linux and
macOS is `bash -e {0}`, which does *not* enable it — only an explicit `shell: bash` gets
`-eo pipefail`. Without `set -o pipefail` in the step body, `tee`'s exit code is the pipeline's, and
a failing `ctest` reports green. That is precisely the false-green this gate exists to prevent, so
the line is written out rather than relied upon:

```yaml
run: |
  set -o pipefail
  ctest --test-dir ... --output-on-failure 2>&1 | tee ci-output/<target>-test-results.txt
```

Windows has no equivalent: `Tee-Object` does not update `$LASTEXITCODE`. The exit code is captured
from a `cmd /c` invocation after the pipeline and re-raised:

```powershell
cmd /c "ctest --test-dir ... -C Release --output-on-failure 2>&1" | Tee-Object -FilePath $results
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

### Trigger — push to `ci/**` only, and nothing from the pull-request family

```yaml
on:
  push:
    branches:
      - 'ci/**'
```

**This repository is public and the runners are personal machines. That combination makes a
pull-request trigger a way for anyone to run their code on someone's hardware.** Anybody can open a
pull request from a fork; a `pull_request` trigger would schedule that fork's code onto the fleet.
With push-only triggers, a fork pull request schedules nothing, because only someone who can push to
this repository can start a run.

**Do not add `pull_request`, `pull_request_target`, `workflow_run`, or a `push` trigger on `main`
without re-adjudicating this.** `workflow_dispatch` is acceptable if a manual run is ever wanted —
it requires write access, so the same property holds.

Excluding `main` costs nothing besides: source reaches `main` only by squash-merge from a `ci/**`
branch that already ran green, so a post-squash run would rebuild identical source for no new
signal.

### One run at a time per branch

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
```

Each runner has one workspace and one fixed build directory per platform, so two runs on the same
branch would fight over both. Superseding the older run makes that impossible.

### The test gate is real from the first commit

`continue-on-error` never appears on a test step. It is defensible only during initial CI iteration
*before any passing baseline exists*, and that window does not apply here: the smoke suite is green
before CI lands, so the baseline exists from the start. A workflow that reports green while
regressions accumulate is worse than no CI.

### Build outside the checkout

- Linux and macOS: `/tmp/kirpich-ci-build`
- Windows: `C:\kirpich-ci-build`

Required because the checkout path contains a space, which breaks a number of Windows build tools.
Building outside the tree removes the risk on every platform.

### Engine submodule requirements

The engine is consumed as a submodule with nested submodules of its own, which imposes:

- **Recursive checkout** — `submodules: recursive`.
- **Token access while the engine repository is private** — a secret plus a `git -c
  url.…insteadOf` rewrite so the submodule descent authenticates.
- **ClangCL on Windows** — the vendored emulator core's Windows shims use `#include_next`, which
  MSVC rejects with C1021. Both Windows jobs configure with the ClangCL toolset.
- **A headless video driver on Linux** — test steps set `SDL_VIDEODRIVER=offscreen`.

### One provisioning script per OS, not per architecture

Each script detects architecture at runtime; architecture-specific download URLs sit as variables at
the top.

- `ci/runner-setup/linux/deps-install.sh` — branches on `uname -m` for `x86_64` vs `aarch64`.
- `ci/runner-setup/macos/deps-install.sh` — Apple Silicon, with detection present so an Intel runner
  can be added without a rewrite.
- `ci/runner-setup/windows/deps-install.ps1` — branches on `$env:PROCESSOR_ARCHITECTURE`; the ARM64
  path additionally installs the ARM64 cross-compilation tools.

**Rejected: per-architecture directories.** They duplicate ~90% of each script for the sake of a few
download URLs, and adding an architecture would mean copying a whole script rather than adding a
branch. New architectures extend the per-OS script.

### Provisioned packages

- **Linux** (apt): `build-essential`, CMake ≥ 3.28, `ninja-build`, `git`, `ccache`, `pkg-config`,
  plus the platform layer's build dependencies (ALSA, PulseAudio, PipeWire, X11, Wayland headers).
- **macOS** (Homebrew): `cmake`, `ninja`, `ccache`, `git`. Audio and video come from the SDK.
- **Windows** (winget): Visual Studio 2022 Build Tools with the C++ workload, CMake, Ninja, Git;
  ARM64 additionally needs the ARM64 toolchain component. The script verifies each and reports a
  clear error when one is missing.

### ccache

Wired through a CMake probe on Linux and macOS; MSVC does not integrate with it. Cache size is set
per host. Stats are reported at the end of each Linux and macOS job with `continue-on-error: true` —
failing to *gather* stats must never fail a job.

### Windows cache hygiene

Windows runs reuse their build directory, and a stale `CMakeCache.txt` can break configuration after
a CMake change. The Windows jobs remove `CMakeCache.txt`, `CMakeFiles/`, and the dependency subbuild
directories, while preserving the downloaded dependency sources — those are slow to refetch and
stable across runs.

### Build type

`Release` on all platforms: bounded build times, and optimization differences can't hide behind a
debug build. A separate Linux job with sanitizers is worth adding once there is meaningful
implementation code to exercise.

### The ROM lives on the runner, never in the repository or the workspace

Work that reads the original ROM — extraction, and later the frame-accuracy comparisons — needs a
ROM in CI. It is never committed, never uploaded, and never placed in the Actions work directory;
`.gitignore` bans ROM extensions tree-wide as a backstop. Each runner holds its own copy outside
the workspace, put there by hand during provisioning.

The location is a fixed convention, not a configured one — one path per platform family, identical
across every runner in that family:

| Platform | Path |
|---|---|
| Linux, macOS | `$HOME/ci-assets/kirpich/tetris.gb` |
| Windows | `C:\ci-assets\kirpich\tetris.gb` |

No environment variable. A convention that is the same on every machine is one less thing to
provision, one less thing to verify, and one less way for a runner to be subtly misconfigured while
appearing healthy. Provisioning is: make the directory, copy the ROM in.

Write `$HOME` rather than a literal `~` in workflow steps — a tilde inside a quoted string is not
expanded by the shell, and quoting is otherwise the right habit for paths.

**Provisioned on all five runners, 2026-08-04.**

**A missing ROM is a provisioning failure, not a skip.** A job that cannot find it fails loudly and
names the path it looked for. Silently skipping would report green while verifying nothing, which
is the failure mode the whole test gate exists to prevent.

Nothing derived from the ROM leaves the runner: extraction output stays in the workspace and is
excluded from uploaded artifacts, and the packaging check refuses any artifact carrying it.

### CI proves extraction works; it must not leave the assets in place

Extraction jobs verify that the decode is **correct on that platform** — right dimensions, right
pixels, byte-identical across platforms — and write their output to a scratch directory that is
discarded. **They never populate `assets/gfx/default/` on the runner.**

This is not tidiness. The canonical asset directory being empty is the precondition for the
first-start flow: a populated one means the presence check passes, the picker never opens, and the
one thing that has to be checked by a human on each platform can no longer be checked at all. A CI
job that populates it silently disables the manual verification it was supposed to support.

So the split is: **CI owns the decode, a person owns the dialog.** SDL's picker is a genuinely
different implementation per platform — Cocoa on macOS, the XDG portal over DBus on Linux, the
Windows shell dialog — and none of them can be exercised by an unattended job. Each platform's
picker gets verified by hand, on that machine, with the asset directory empty.

The same rule applies to any future job that runs the game: leave the runner in a state where
launching the binary still reaches the picker.

## Implementation details

- `.github/workflows/ci.yml` — five jobs (`build-linux`, `build-macos`, `build-windows-msvc`,
  `build-linux-arm64`, `build-windows-arm64`); ARM64 chain via `needs:` + `if: ${{ always() }}`;
  per-job test capture and artifact upload.
- `ci/runner-setup/linux/deps-install.sh`
- `ci/runner-setup/macos/deps-install.sh`
- `ci/runner-setup/windows/deps-install.ps1`

## Open questions

- **Which environments can show a file dialog at all.** The picker needs a desktop session — on
  Linux it talks to the XDG portal over DBus, which a headless machine does not have. The three
  ARM64 targets share one Apple Silicon host as VMs; whether those VMs have desktop sessions
  decides whether their pickers can be verified by hand or are simply out of reach. If they are
  headless, ARM64 gets its decode verified and its dialog unverified, and that gap is stated rather
  than papered over.
- **Which targets actually need the ROM.** Provisioning is per machine, and the Apple Silicon host
  needs it reachable from three environments. Worth settling whether extraction is verified on every
  target or a representative subset first: the decode is Python stdlib byte manipulation plus
  `zlib`, so cross-platform variance is low and the per-target cost may exceed what it catches.
- **Sanitizer job.** A `Debug` build with address and undefined-behavior sanitizers on Linux x64,
  added once game systems land.
- **Dependency download caching.** Compiler caching already covers the build step; if fetching
  dependencies becomes the hot spot, a runner-side cache of the source trees is the next move.
- **Disk usage on the Windows ARM64 VM.** The C++ workload plus ARM64 cross-tools is around 10 GB;
  worth monitoring.
