# Copies the per-rank logs written on the remote hosts back to this (rank 0) host.
#
# Every non-rank-0 host of the cluster json writes run_r<k>.csv (and the node benchmarks bench_* / nvsmi_*)
# under its own copy of the repo. They are fetched over the administrative share \\<ip>\C$ - run this from
# the PowerShell window of the local account "hpc" (same account and password on all hosts, see
# docs/instalacja-windows.md, Krok 7). The first host of the json is rank 0 and is skipped.
#
# Usage (from the repo root on the rank-0 host):
#   .\scripts\collect-results.ps1 -Cluster data\cluster.example.json -Path results\runs
#   .\scripts\collect-results.ps1 -Cluster data\cluster.example.json -Path results\bench
param(
    [string]$Cluster = "data\cluster.example.json",
    [string]$Path = "results\runs"
)

$ErrorActionPreference = "Stop"
$cfg = Get-Content -Raw -Encoding UTF8 $Cluster | ConvertFrom-Json
$repo = $cfg.repo_path
if ($repo -notmatch '^[A-Za-z]:\\') { throw "repo_path must start with a drive letter: $repo" }
$share = "$($repo.Substring(0, 1))`$$($repo.Substring(2))"   # C:\hpc\repo -> C$\hpc\repo

$failed = 0
foreach ($h in ($cfg.hosts | Select-Object -Skip 1)) {
    $src = "\\$($h.host)\$share\$Path"
    if (-not (Test-Path $src)) {
        Write-Host "[skip] $($h.host) ($($h.name)): $src not found"
        continue
    }
    Write-Host "[copy] $($h.host) ($($h.name)): $src -> $Path"
    & robocopy $src $Path "run_r*.csv" "nvsmi_*.csv" "bench_*.*" /S /XO /NJH /NJS /NP /R:1 /W:1
    # robocopy exit codes below 8 mean success (0 = nothing to copy, 1 = files copied, 2-7 = extras/mismatches)
    if ($LASTEXITCODE -ge 8) {
        Write-Host "[FAIL] $($h.host): robocopy exit code $LASTEXITCODE"
        $failed++
    }
}
if ($failed -gt 0) { exit 1 }
exit 0
