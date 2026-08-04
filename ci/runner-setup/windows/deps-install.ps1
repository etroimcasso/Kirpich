# Windows self-hosted runner — build-dependency setup. Run ONCE per runner, in an ELEVATED
# PowerShell (Administrator).
#
# One script for both architectures. It branches on $env:PROCESSOR_ARCHITECTURE only where something
# genuinely differs — the ARM64 machine additionally needs the ARM64 cross-tools component. Nothing
# here touches the runner service or its labels.
#
# A bare Windows box ships execution policy = Restricted, which refuses to run this at all. Invoke
# it with the bypass flag (process scope — always effective, no persistent change):
#     powershell -ExecutionPolicy Bypass -File .\deps-install.ps1
#
# Installs, all via winget:
#   - Git                    (actions/checkout, and the engine submodule init)
#   - CMake 3.28+
#   - Ninja                  (convenience; the jobs use the Visual Studio generator)
#   - Visual Studio 2022 Build Tools with the C++ workload, the ClangCL toolset, and the Windows SDK
#
# ClangCL rather than MSVC is not a preference: SameBoy's Windows shims use #include_next, which
# MSVC rejects outright (C1021). The jobs configure with -T ClangCL, so the toolset must be present.
#
# SDL3, SameBoy, spdlog and GoogleTest are built from source (submodule or FetchContent) — nothing
# to install for them.

$ErrorActionPreference = "Stop"

$arch = $env:PROCESSOR_ARCHITECTURE
Write-Host "== Kirpich runner setup — Windows $arch =="

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget not found. Install 'App Installer' from the Microsoft Store (or update Windows), then re-run."
}

Write-Host "== Git =="
winget install --id Git.Git --exact --silent `
    --accept-source-agreements --accept-package-agreements

Write-Host "== CMake =="
winget install --id Kitware.CMake --exact --silent `
    --accept-source-agreements --accept-package-agreements

Write-Host "== Ninja =="
winget install --id Ninja-build.Ninja --exact --silent `
    --accept-source-agreements --accept-package-agreements

# --override passes the VS installer its own component switches; --includeRecommended pulls the
# matching MSVC + CRT. The ARM64 machine gets the ARM64 build tools on top of the x86/x64 set.
$vsComponents = @(
    "--add Microsoft.VisualStudio.Workload.VCTools"
    "--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
    "--add Microsoft.VisualStudio.Component.VC.Llvm.Clang"
    "--add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset"
    "--add Microsoft.VisualStudio.Component.Windows11SDK.22621"
)
if ($arch -eq "ARM64") {
    Write-Host "   (ARM64 host — adding the ARM64 build tools)"
    $vsComponents += "--add Microsoft.VisualStudio.Component.VC.Tools.ARM64"
}

Write-Host "== Visual Studio 2022 Build Tools (C++ + ClangCL + Windows SDK) =="
$override = "--quiet --wait --norestart --nocache " + ($vsComponents -join " ") + " --includeRecommended"
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --silent `
    --accept-source-agreements --accept-package-agreements `
    --override $override

Write-Host ""
Write-Host "Done. Open a NEW terminal so PATH refreshes, then verify:"
Write-Host "    cmake --version     # 3.28 or newer"
Write-Host "    git --version"
Write-Host ""
Write-Host "Then place the ROM (see docs/features/ci.md):"
Write-Host "    New-Item -ItemType Directory -Force C:\ci-assets\kirpich"
Write-Host "    Copy-Item <your-rom>.gb C:\ci-assets\kirpich\tetris.gb"
