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
| Linux x64 | `[self-hosted, Linux, X64]` | GCC ≥ 13 | Ninja |
| macOS ARM64 | `[self-hosted, macOS, ARM64]` | Apple Clang | Ninja |
| Linux ARM64 | `[self-hosted, Linux, ARM64]` | GCC ≥ 13 | Ninja |
| Windows x64 | `[self-hosted, X64, Windows]` | ClangCL (VS 2022) | Visual Studio 17 2022, x64 |
| Windows ARM64 | `[self-hosted, Windows, ARM64]` | ClangCL (VS 2022 + ARM64 tools) | Visual Studio 17 2022, ARM64 |

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

The default POSIX shell sets `pipefail`, so the step's exit code reflects `ctest`, not `tee` — a
test failure correctly fails the step. The Windows `cmd /c` + `Tee-Object` chain forwards the inner
exit code the same way.

### Trigger

```yaml
on:
  push:
    branches: ['ci/**']
  pull_request:
    branches: [main]
```

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

## Implementation details

- `.github/workflows/ci.yml` — five jobs (`build-linux`, `build-macos`, `build-windows-msvc`,
  `build-linux-arm64`, `build-windows-arm64`); ARM64 chain via `needs:` + `if: ${{ always() }}`;
  per-job test capture and artifact upload.
- `ci/runner-setup/linux/deps-install.sh`
- `ci/runner-setup/macos/deps-install.sh`
- `ci/runner-setup/windows/deps-install.ps1`

## Open questions

- **Sanitizer job.** A `Debug` build with address and undefined-behavior sanitizers on Linux x64,
  added once game systems land.
- **Dependency download caching.** Compiler caching already covers the build step; if fetching
  dependencies becomes the hot spot, a runner-side cache of the source trees is the next move.
- **Disk usage on the Windows ARM64 VM.** The C++ workload plus ARM64 cross-tools is around 10 GB;
  worth monitoring.
