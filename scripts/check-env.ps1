# Verify the toolchain required by the project. Prints OK/FAIL per item and
# exits 1 if anything required is missing. Does not modify the system.
#
#   .\scripts\check-env.ps1

$CudaVersion = '12.9'
$MinDriver = 576.02   # minimum Windows driver for CUDA 12.9
$failed = 0

function Report($name, $ok, $detail) {
    if ($ok) { Write-Host ("[ OK ] {0,-28} {1}" -f $name, $detail) -ForegroundColor Green }
    else     { Write-Host ("[FAIL] {0,-28} {1}" -f $name, $detail) -ForegroundColor Red; $script:failed++ }
}

function Has($cmd) { [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }

Report 'git' (Has git) $(if (Has git) { (git --version) } else { 'winget install -e --id Git.Git' })
Report 'uv' (Has uv) $(if (Has uv) { (uv --version) } else { 'winget install -e --id astral-sh.uv' })

$cmakeOk = $false; $cmakeInfo = 'winget install -e --id Kitware.CMake'
if (Has cmake) {
    $ver = [version]((cmake --version | Select-Object -First 1) -replace '[^0-9.]', '')
    $cmakeOk = $ver -ge [version]'3.24'; $cmakeInfo = "cmake $ver (need >= 3.24)"
}
Report 'cmake' $cmakeOk $cmakeInfo

$drvOk = $false; $drvInfo = 'nvidia-smi not found: install/update the NVIDIA driver'
if (Has nvidia-smi) {
    $q = (nvidia-smi --query-gpu=name,driver_version --format=csv,noheader | Select-Object -First 1).Split(',')
    $drvOk = [double]$q[1].Trim() -ge $MinDriver
    $drvInfo = "$($q[0].Trim()), driver $($q[1].Trim()) (need >= $MinDriver)"
}
Report 'NVIDIA driver' $drvOk $drvInfo

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs2022 = $null
if (Test-Path $vswhere) {
    $vs2022 = & $vswhere -products * -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath
}
Report 'VS 2022 C++ tools' ([bool]$vs2022) $(if ($vs2022) { $vs2022 } else { 'run scripts\setup-windows.ps1' })

$nvccOk = $false; $nvccInfo = "CUDA $CudaVersion not found"
if (Has nvcc) {
    $line = nvcc --version | Select-String 'release'
    $nvccOk = "$line" -match "release $([regex]::Escape($CudaVersion))"
    $nvccInfo = "$line".Trim()
}
Report "nvcc $CudaVersion" $nvccOk $nvccInfo

$integ = $false
if ($vs2022) {
    $integ = Test-Path "$vs2022\MSBuild\Microsoft\VC\v170\BuildCustomizations\CUDA $CudaVersion.props"
}
Report 'CUDA <-> VS 2022 integration' $integ $(if ($integ) { 'BuildCustomizations present' }
    elseif (-not $vs2022 -or -not $nvccOk) { 'needs VS 2022 and CUDA first' }
    else { 'reinstall CUDA AFTER VS 2022 Build Tools (winget uninstall Nvidia.CUDA, then rerun setup)' })

# CUDA's MSBuild integration reads CUDA_PATH_V<major>_<minor> from the process environment;
# a terminal started before the CUDA install still has the old (empty) value.
$cudaVar = 'CUDA_PATH_V' + ($CudaVersion -replace '\.', '_')
$machineCuda = [Environment]::GetEnvironmentVariable($cudaVar, 'Machine')
$shellCuda = [Environment]::GetEnvironmentVariable($cudaVar, 'Process')
Report "$cudaVar in this shell" ([bool]$shellCuda) $(if ($shellCuda) { $shellCuda }
    elseif ($machineCuda) { 'set system-wide but not in this shell: close ALL terminal windows (or reboot) and reopen' }
    else { 'not set: install CUDA (scripts\setup-windows.ps1)' })

Report 'MS-MPI runtime (mpiexec)' (Has mpiexec) $(if (Has mpiexec) { (Get-Command mpiexec).Source } else { 'winget install -e --id Microsoft.msmpi' })
$machineMpi = [Environment]::GetEnvironmentVariable('MSMPI_INC', 'Machine')
Report 'MS-MPI SDK' ([bool]$env:MSMPI_INC -and (Test-Path "$env:MSMPI_INC\mpi.h")) $(if ($env:MSMPI_INC) { $env:MSMPI_INC }
    elseif ($machineMpi) { 'set system-wide but not in this shell: close ALL terminal windows (or reboot) and reopen' }
    else { 'winget install -e --id Microsoft.msmpisdk' })

$nsys = Get-ChildItem "$env:ProgramFiles\NVIDIA Corporation\Nsight Systems*\target-windows-x64\nsys.exe" -ErrorAction SilentlyContinue | Select-Object -Last 1
$ncu = Get-ChildItem "$env:ProgramFiles\NVIDIA Corporation\Nsight Compute*\ncu.bat" -ErrorAction SilentlyContinue | Select-Object -Last 1
Report 'Nsight Systems (nsys)' ([bool]$nsys) $(if ($nsys) { $nsys.FullName } else { 'installed together with CUDA' })
Report 'Nsight Compute (ncu)' ([bool]$ncu) $(if ($ncu) { $ncu.FullName } else { 'installed together with CUDA' })

if ($failed) { Write-Host "`n$failed check(s) failed." -ForegroundColor Red; exit 1 }
Write-Host "`nEnvironment OK." -ForegroundColor Green
