# One-shot developer setup for a fresh Windows 11 machine (run from the repo root
# in an elevated PowerShell). Installs the pinned toolchain with winget, then runs
# scripts/check-env.ps1. Safe to re-run: already installed packages are skipped.
#
#   Set-ExecutionPolicy -Scope Process Bypass; .\scripts\setup-windows.ps1
#
# Order matters: Visual Studio 2022 Build Tools must be installed BEFORE CUDA,
# otherwise the CUDA installer cannot add its MSBuild integration to VS 2022.

$ErrorActionPreference = 'Stop'

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'Run this script from PowerShell started "as Administrator".'
}
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Error 'winget not found. Install "App Installer" from Microsoft Store and retry.'
}

# Pinned versions: identical on every cluster node.
$CudaVersion = '12.9'
$Packages = @(
    @{ Id = 'Git.Git';                               Args = @() },
    @{ Id = 'Kitware.CMake';                         Args = @() },
    @{ Id = 'astral-sh.uv';                          Args = @() },
    @{ Id = 'Microsoft.VisualStudio.2022.BuildTools'; Args = @('--override',
        '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended') },
    @{ Id = 'Nvidia.CUDA';                           Args = @('--version', $CudaVersion) },
    @{ Id = 'Microsoft.msmpi';                       Args = @() },
    @{ Id = 'Microsoft.msmpisdk';                    Args = @() }
)

foreach ($p in $Packages) {
    Write-Host "`n=== $($p.Id) ===" -ForegroundColor Cyan
    $installed = winget list --exact --id $p.Id --accept-source-agreements 2>$null | Select-String -SimpleMatch $p.Id
    if ($installed) {
        Write-Host "already installed, skipping"
        continue
    }
    winget install --exact --id $p.Id --silent --accept-package-agreements --accept-source-agreements @($p.Args)
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "winget returned $LASTEXITCODE for $($p.Id); check-env.ps1 below will tell if something is missing."
    }
}

# Pick up PATH / MSMPI_* changes made by the installers without reopening the shell.
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path', 'User')
foreach ($v in 'MSMPI_INC', 'MSMPI_LIB64', 'MSMPI_BIN', 'CUDA_PATH') {
    $val = [Environment]::GetEnvironmentVariable($v, 'Machine')
    if ($val) { Set-Item "env:$v" $val }
}

Write-Host "`n=== uv sync ===" -ForegroundColor Cyan
uv sync

& "$PSScriptRoot\check-env.ps1"
