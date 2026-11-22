# Per-node GPU benchmark: hardware description, population-size sweep and a sustained run (thermal check).
# Runs LOCALLY on one host, from anywhere (it changes to the repo root; -OutDir is relative to it). Remote launch from the rank-0 host,
# from the PowerShell of the account "hpc" (hosts by IP, see docs/kampania-iteracja-1.md):
#   mpiexec -timeout 900 -genv MPICH_NETMASK $NM -hosts 1 <ip> 1 powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-node.ps1 -Instance qft_n12 -OutDir results\bench
#
# Files written to -OutDir (all prefixed with the host name):
#   bench_<host>_node.json       hardware, driver, CUDA, OS, NIC, top CPU consumers at start
#   bench_<host>_top_cpu_*.txt   top 10 processes by CPU use (2 s window) at start and at end
#   nvsmi_<host>.csv             nvidia-smi samples every 500 ms for the whole script
#   bench_<host>_pop<P>_r0.csv   qmap_ga per-generation log of the sweep step with population P
#   bench_<host>_sweep.csv       one row per step (sweep and sustain): timestamps, times, throughput, status
#   bench_<host>_sustain_r0.csv  qmap_ga log of the sustained run
# Sweep rule (Windows TDR safety): once a step shows one evaluation (eval_time_s) longer than 1.0 s, all larger
# populations are skipped. The sustained run uses -SustainPop (default: the cluster pop of this host).
param(
    [string]$Instance = "qft_n12",
    [string]$OutDir = "results\bench",
    [int]$SustainPop = 262144,
    [int]$SustainSeconds = 120
)

$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
$hostName = $env:COMPUTERNAME
$exe = Join-Path $repo "build\Release\qmap_ga.exe"
$device = "data\devices\falcon27.json"
$qasm = "data\instances\$Instance.qasm"
$pops = @(4096, 16384, 65536, 262144, 1048576, 4194304)
$tdrLimitS = 1.0
New-Item -ItemType Directory -Force $OutDir | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-Text([string]$path, [string]$text) {
    [System.IO.File]::WriteAllText((Join-Path $repo $path), $text, $utf8)
}

# When started by mpiexec this process inherits the MPI job environment; qmap_ga would then try to join that job
# instead of running as an MPI singleton. Remove it (MS-MPI/PMI variables of the parent job).
$removed = @()
foreach ($e in (Get-ChildItem Env:)) {
    if ($e.Name -match '^(PMI_|MPIEXEC_|MPICH_)' -or
        ($e.Name -match '^MSMPI_' -and $e.Name -notmatch '^MSMPI_(BIN|INC|LIB32|LIB64)$')) {
        Remove-Item "Env:$($e.Name)" -ErrorAction SilentlyContinue
        $removed += $e.Name
    }
}
Write-Host "[env] removed MPI job variables: $($removed -join ', ')"

function Get-TopCpu {
    # CPU share of the whole machine over a 2 s window, top 10 processes.
    $logical = [Environment]::ProcessorCount
    $a = @{}
    foreach ($p in (Get-Process)) { $a[$p.Id] = [double]($p.CPU) }
    Start-Sleep -Seconds 2
    $rows = foreach ($p in (Get-Process)) {
        $before = 0.0
        if ($a.ContainsKey($p.Id)) { $before = $a[$p.Id] }
        [pscustomobject]@{
            name = $p.ProcessName
            id = $p.Id
            cpu_percent_of_total = [math]::Round((([double]($p.CPU) - $before) / 2.0) / $logical * 100.0, 2)
            cpu_seconds_total = [math]::Round([double]($p.CPU), 1)
        }
    }
    $rows | Sort-Object cpu_percent_of_total -Descending | Select-Object -First 10
}

function Save-TopCpu([string]$tag) {
    $top = Get-TopCpu
    Write-Text "$OutDir\bench_${hostName}_top_cpu_$tag.txt" (($top | Format-Table -AutoSize | Out-String))
    return $top
}

# ---------------------------------------------------------------- node description
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$sys = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
$gpuFields = "name,memory.total,driver_version,pcie.link.gen.max,pcie.link.gen.current,pcie.link.width.max,pcie.link.width.current,clocks.max.sm,power.limit"
$gpu = @{}
try {
    $line = (& nvidia-smi -i 0 "--query-gpu=$gpuFields" --format=csv,noheader,nounits 2>&1 | Select-Object -First 1)
    $vals = $line -split ',' | ForEach-Object { $_.Trim() }
    $keys = $gpuFields -split ','
    for ($i = 0; $i -lt $keys.Count -and $i -lt $vals.Count; $i++) { $gpu[$keys[$i]] = $vals[$i] }
} catch { Write-Host "[warn] nvidia-smi query failed: $_" }

$nvcc = (Get-Command nvcc -ErrorAction SilentlyContinue)
$nvccPath = if ($nvcc) { $nvcc.Source } elseif ($env:CUDA_PATH) { Join-Path $env:CUDA_PATH "bin\nvcc.exe" } else { "" }
$nvccRelease = ""
if ($nvccPath -and (Test-Path $nvccPath)) {
    $nvccRelease = (& $nvccPath --version 2>&1 | Select-String 'release' | Select-Object -First 1).ToString().Trim()
}

# the NIC that carries the 192.168.66.0/24 cluster network
$nic = $null
$ip = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -like '192.168.66.*' } | Select-Object -First 1
if ($ip) { $nic = Get-NetAdapter -InterfaceIndex $ip.InterfaceIndex -ErrorAction SilentlyContinue }

$topStart = Save-TopCpu "start"
$node = [ordered]@{
    hostname = $hostName
    cpu = $cpu.Name.Trim()
    cpu_cores = $cpu.NumberOfCores
    cpu_threads = $cpu.NumberOfLogicalProcessors
    ram_gb = [math]::Round($sys.TotalPhysicalMemory / 1GB, 1)
    gpu = $gpu
    os = "$($os.Caption) $($os.Version) build $($os.BuildNumber)"
    nic = if ($nic) { [ordered]@{ name = $nic.Name; description = $nic.InterfaceDescription; link_speed = $nic.LinkSpeed; ip = $ip.IPAddress } } else { $null }
    nvcc = $nvccRelease
    instance = $Instance
    sustain_pop = $SustainPop
    top_cpu_at_start = @($topStart)
    mpi_env_removed = $removed
    started = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
}
Write-Text "$OutDir\bench_${hostName}_node.json" (($node | ConvertTo-Json -Depth 5) + "`n")
Write-Host "[node] $($node.cpu), $($gpu['name']), driver $($gpu['driver_version'])"

# ---------------------------------------------------------------- background nvidia-smi sampling
function Start-NvSmiLog([string]$file) {
    foreach ($reasons in @("clocks_throttle_reasons.active", "clocks_event_reasons.active")) {
        $q = "timestamp,temperature.gpu,clocks.sm,clocks.mem,power.draw,utilization.gpu,pstate,$reasons"
        $p = Start-Process -FilePath "nvidia-smi" -PassThru -WindowStyle Hidden -ArgumentList @(
            "--query-gpu=$q", "--format=csv", "-lms", "500", "-f", $file)
        Start-Sleep -Milliseconds 1500
        if (-not $p.HasExited) { return $p }   # the driver knows this field name
    }
    return $null
}
$smi = Start-NvSmiLog (Join-Path $repo "$OutDir\nvsmi_$hostName.csv")
if (-not $smi) { Write-Host "[warn] nvidia-smi logging did not start" }

# ---------------------------------------------------------------- qmap_ga steps
$sweep = New-Object System.Collections.Generic.List[string]
$sweep.Add("phase,pop,start,end,generations,best,mean_gen_time_s,mean_eval_time_s,max_eval_time_s,evals_per_s,status")
$stamp = { (Get-Date).ToString("yyyy/MM/dd HH:mm:ss.fff") }

function Invoke-Step([string]$phase, [int]$pop, [int]$generations, [double]$maxTime, [string]$logBase) {
    $start = & $stamp
    $log = Join-Path $repo ($logBase -replace '\.csv$', '_r0.csv')   # qmap_ga appends _r<rank> to --log
    $status = "ok"
    $row = @("", "", "", "", "", "")
    try {
        if (Test-Path $log) { Remove-Item $log -Force }
        & $exe --instance $qasm --device $device --pop $pop --generations $generations --max-time $maxTime --seed 1 --log (Join-Path $repo $logBase) | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "qmap_ga exit code $LASTEXITCODE" }
        $rows = @(Import-Csv $log | Where-Object { [int]$_.generation -ge 1 })   # generation 0 includes CUDA warm-up
        if ($rows.Count -eq 0) { throw "no generations logged" }
        $last = $rows[-1]
        $meanGen = ($rows | ForEach-Object { [double]$_.gen_time_s } | Measure-Object -Average).Average
        $meanEval = ($rows | ForEach-Object { [double]$_.eval_time_s } | Measure-Object -Average).Average
        $maxEval = ($rows | ForEach-Object { [double]$_.eval_time_s } | Measure-Object -Maximum).Maximum
        $row = @($last.generation, $last.best, ("{0:F6}" -f $meanGen), ("{0:F6}" -f $meanEval), ("{0:F6}" -f $maxEval), $last.evals_per_s)
        if ($maxEval -gt $tdrLimitS) { $status = "slow" }
    } catch {
        Write-Host "[warn] $phase pop=$pop failed: $_"
        $status = "failed"
        $row = @("", "", "", "", "", "")
    }
    $end = & $stamp
    $sweep.Add("$phase,$pop,$start,$end,$($row -join ','),$status")
    return $status
}

$skipRest = $false
foreach ($pop in $pops) {
    if ($skipRest) {
        $sweep.Add("sweep,$pop,,,,,,,,,skipped")
        Write-Host "[skip] pop=$pop (a smaller population exceeded $tdrLimitS s per evaluation)"
        continue
    }
    Write-Host "[sweep] pop=$pop"
    $status = Invoke-Step "sweep" $pop 30 120 "$OutDir\bench_${hostName}_pop$pop.csv"
    if ($status -eq "slow") { $skipRest = $true }
}

Write-Host "[sustain] pop=$SustainPop for $SustainSeconds s"
[void](Invoke-Step "sustain" $SustainPop 100000000 $SustainSeconds "$OutDir\bench_${hostName}_sustain.csv")

# ---------------------------------------------------------------- finish
if ($smi -and -not $smi.HasExited) { Stop-Process -Id $smi.Id -Force -ErrorAction SilentlyContinue }
Write-Text "$OutDir\bench_${hostName}_sweep.csv" (($sweep -join "`n") + "`n")
[void](Save-TopCpu "end")
Write-Host "[done] results in $OutDir (bench_${hostName}_*, nvsmi_${hostName}.csv)"
exit 0
