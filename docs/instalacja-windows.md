# Instalacja środowiska (Windows 11) – krok po kroku

Ta instrukcja jest dla każdego z nas. Robimy ją na każdym z 3 komputerów (RTX 5080, RTX 5070 Laptop, RTX 3060).
Wersje są przypięte, żeby wszystkie węzły klastra miały identyczny toolchain.

| Składnik | Wersja | Po co |
|---|---|---|
| Git | najnowszy | klonowanie repo |
| uv | najnowszy | Python, Qiskit, testy (`uv sync`, `uv run`) |
| CMake | ≥ 3.24 | budowanie części C++/CUDA |
| Visual Studio 2022 Build Tools (C++) | 17.x | kompilator MSVC dla `nvcc` |
| CUDA Toolkit | **12.9** | `nvcc`, Nsight Systems, Nsight Compute |
| MS-MPI (runtime + SDK) | 10.1 | `mpiexec`, `mpi.h`, migracja między wyspami |
| Sterownik NVIDIA | ≥ 576.02 | wymagany przez CUDA 12.9 |

Dlaczego **VS 2022**, skoro jest już VS 2026? `nvcc` z VS 2026 (MSVC 19.50) ma zgłoszone błędy
(„Host compiler targets unsupported OS”, awarie `cudafe++`). VS 2022 jest wspierane przez CUDA 12.9 bez zastrzeżeń.
Obie wersje VS mogą być zainstalowane jednocześnie, a projekt jawnie wybiera VS 2022.

Dlaczego **CUDA 12.9**, a nie 13.x? Obsługuje obie nasze architektury (sm_86 dla RTX 3060, sm_120 dla RTX 50xx).
Wymaga też starszego sterownika niż 13.x, co ma znaczenie przy laptopie, gdzie sterowniki producenta bywają opóźnione.

## Krok 0. Sterownik NVIDIA

1. Otwórz PowerShell i wpisz `nvidia-smi`. W prawym górnym rogu jest „Driver Version”.
2. Jeżeli wersja jest niższa niż **576.02**, zaktualizuj sterownik przez aplikację NVIDIA App
   albo ze strony <https://www.nvidia.com/drivers>. Potem zrestartuj komputer.

## Krok 1. Sklonuj repozytorium

Jeśli nie masz Gita: `winget install -e --id Git.Git`, potem zamknij i otwórz PowerShell ponownie.

```powershell
cd E:\        # albo dowolny katalog
git clone <adres-repo> HPC-quantum-transpilaton
cd HPC-quantum-transpilaton
```

## Krok 2. Uruchom skrypt instalacyjny (jako administrator)

1. Kliknij Start, wpisz „PowerShell”, wybierz **Uruchom jako administrator**.
2. Przejdź do katalogu repo i uruchom:

```powershell
cd E:\HPC-quantum-transpilaton
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\setup-windows.ps1
```

Skrypt instaluje kolejno (przez `winget`): Git, CMake, uv, **VS 2022 Build Tools z C++**, **CUDA 12.9**,
MS-MPI runtime i MS-MPI SDK. Na końcu robi `uv sync` i uruchamia sprawdzenie środowiska.

- Trwa to 15–40 minut (VS Build Tools i CUDA to kilka GB). Okienka instalatorów mogą się pojawiać – nic nie klikaj,
  chyba że instalator wprost o coś pyta.
- Kolejność jest ważna: VS 2022 **przed** CUDA, bo instalator CUDA dodaje integrację z MSBuild tylko do już
  zainstalowanych wersji VS. Skrypt pilnuje tej kolejności.
- Skrypt można uruchomić ponownie: pomija to, co już jest zainstalowane.

## Krok 3. Zrestartuj komputer

Po instalacji CUDA i MS-MPI zrób restart (zmienne środowiskowe `PATH`, `CUDA_PATH`, `MSMPI_*` i usługa MS-MPI).

## Krok 4. Sprawdź środowisko

W **zwykłym** PowerShellu (nie musi być administrator), w katalogu repo:

```powershell
.\scripts\check-env.ps1
```

Oczekiwany wynik: same `[ OK ]` i na końcu `Environment OK.` Przykład:

```
[ OK ] git                          git version 2.53.0.windows.1
[ OK ] uv                           uv 0.12.23
[ OK ] cmake                        cmake 4.2.3 (need >= 3.24)
[ OK ] NVIDIA driver                NVIDIA GeForce RTX 5080, driver 621.01 (need >= 576.02)
[ OK ] VS 2022 C++ tools            C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
[ OK ] nvcc 12.9                    Cuda compilation tools, release 12.9, V12.9.x
[ OK ] CUDA <-> VS 2022 integration BuildCustomizations present
[ OK ] MS-MPI runtime (mpiexec)     C:\Program Files\Microsoft MPI\Bin\mpiexec.exe
[ OK ] MS-MPI SDK                   C:\Program Files (x86)\Microsoft SDKs\MPI\Include\
[ OK ] Nsight Systems (nsys)        ...
[ OK ] Nsight Compute (ncu)         ...
```

Każda linia `[FAIL]` podaje obok, co zrobić.

## Krok 5. Zbuduj projekt i uruchom testy

```powershell
uv run pytest -q
cmake -S cpp -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`-G "Visual Studio 17 2022"` jest ważne na komputerach, gdzie jest też VS 2026: bez tego CMake wybierze
najnowsze VS. Jeśli katalog `build` był wcześniej skonfigurowany innym generatorem, usuń go
(`Remove-Item -Recurse -Force build`) i skonfiguruj ponownie.

## Krok 6 (tylko do profilowania). Liczniki GPU dla Nsight Compute

Bez tego `ncu` na kartach GeForce zgłasza `ERR_NVGPUCTRPERM`.

1. Prawy klik na pulpicie → **Panel sterowania NVIDIA**.
2. Menu **Pulpit** → zaznacz **Włącz ustawienia dewelopera**.
3. **Deweloper → Zarządzaj licznikami wydajności GPU** → **Zezwalaj na dostęp wszystkim użytkownikom**.

## Krok 7. Klaster MPI (kilka komputerów)

Procedura sprawdzona 2026-11-22 na naszych trzech komputerach (grupa robocza Windows, bez domeny):

| Węzeł | Host | IP |
|---|---|---|
| RTX 5080 (rank 0, uruchamia `mpiexec`) | AMG-NORTH | 192.168.66.61 |
| RTX 3060 | PS04 | 192.168.66.2 |
| RTX 5070 Laptop | HyperX_Zuber | 192.168.66.222 (wbudowana karta Ethernet; do 2026-11-22: .169, karta USB) |

W naszej sieci **nazwy hostów nie są rozwiązywane**, więc wszędzie używamy adresów IP. Konfiguracja dla
`qmap.experiments plan`: [`data/cluster.example.json`](../data/cluster.example.json).

Na **każdym** komputerze:

1. **Sieć prywatna, kabel.** Profil sieci „Prywatna”
   (`Set-NetConnectionProfile -InterfaceAlias "<nazwa>" -NetworkCategory Private`). Na laptopie na czas pomiarów
   wyłącz Wi-Fi (`Disable-NetAdapter -Name "Wi-Fi" -Confirm:$false`), żeby ruch MPI nie szedł przez drugi adres.
2. **Ta sama ścieżka repozytorium: `C:\hpc\HPC-quantum-transpilaton`**, ten sam commit i zbudowany projekt
   (Krok 5). `mpiexec` uruchamia zdalnie program pod tą samą ścieżką i z tym samym katalogiem roboczym.
   Jeśli repo leży gdzie indziej, zrób dowiązanie (cmd jako administrator):
   `mklink /J C:\hpc\HPC-quantum-transpilaton E:\ścieżka\do\repo`.
3. **Wspólne konto `hpc`** z tym samym hasłem na wszystkich komputerach (bez domeny Windows uwierzytelnia
   zdalnego użytkownika tylko wtedy, gdy u celu istnieje konto o tej samej nazwie i haśle; inaczej
   `mpiexec` kończy się błędem „Odmowa dostępu (errno 5)”). PowerShell jako administrator:
   ```powershell
   $p = Read-Host "Haslo dla konta hpc (TO SAMO na wszystkich komputerach)" -AsSecureString
   New-LocalUser -Name hpc -Password $p -PasswordNeverExpires -AccountNeverExpires -FullName "HPC MPI"
   Add-LocalGroupMember -SID S-1-5-32-544 -Member hpc   # Administratorzy; SID działa w każdej wersji językowej
   New-ItemProperty -Path HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System -Name LocalAccountTokenFilterPolicy -Value 1 -PropertyType DWord -Force
   ```
4. **Zapora** (PowerShell jako administrator), profil prywatny. Reguła działa na prawdziwą ścieżkę pliku:
   jeśli `C:\hpc\...` jest dowiązaniem, dodaj regułę także dla ścieżki docelowej.
   ```powershell
   $repo = "C:\hpc\HPC-quantum-transpilaton"
   New-NetFirewallRule -DisplayName "MS-MPI smpd" -Direction Inbound -Action Allow -Profile Private -Program "C:\Program Files\Microsoft MPI\Bin\smpd.exe"
   New-NetFirewallRule -DisplayName "MS-MPI mpiexec" -Direction Inbound -Action Allow -Profile Private -Program "C:\Program Files\Microsoft MPI\Bin\mpiexec.exe"
   New-NetFirewallRule -DisplayName "qmap_ga" -Direction Inbound -Action Allow -Profile Private -Program "$repo\build\Release\qmap_ga.exe"
   New-NetFirewallRule -DisplayName "qmap_mpi_tests" -Direction Inbound -Action Allow -Profile Private -Program "$repo\build\Release\qmap_mpi_tests.exe"
   New-NetFirewallRule -DisplayName "qmap_mpi_bench" -Direction Inbound -Action Allow -Profile Private -Program "$repo\build\Release\qmap_mpi_bench.exe"
   New-NetFirewallRule -DisplayName "ICMPv4 ping" -Direction Inbound -Action Allow -Profile Private -Protocol ICMPv4 -IcmpType 8
   ```
5. **Demon `smpd` jako `hpc`** – na **wszystkich** komputerach, łącznie z tym, z którego uruchamiamy `mpiexec`
   (bez tego: „Serwer RPC jest niedostępny (errno 1722)” dla własnego adresu). Okno zostaje otwarte przez
   całą sesję pomiarów:
   ```powershell
   runas /user:hpc "powershell -NoExit -Command & 'C:\Program Files\Microsoft MPI\Bin\smpd.exe' -d"
   ```

Na komputerze rank 0 komendy `mpiexec` uruchamiamy w oknie konta `hpc` (`runas /user:hpc powershell`)
z katalogu `C:\hpc\HPC-quantum-transpilaton`. **Zawsze** z `-genv MPICH_NETMASK 192.168.66.0/255.255.255.0`:

- `MPICH_NETMASK` ogranicza MS-MPI do sieci LAN. Bez niego proces na komputerze z wirtualnymi kartami
  (Hyper-V `vEthernet`, VirtualBox host-only) lub z Wi-Fi ogłasza nieosiągalny adres i połączenia między
  rankami wiszą do przekroczenia `-timeout`.
- **`-genv`, nie `-env`.** W postaci MPMD (bloki oddzielone `:`) `-env` dotyczy tylko pierwszego bloku –
  pozostałe ranki nie dostają maski i program wisi.

Testy łączności (od najprostszego):

```powershell
cd C:\hpc\HPC-quantum-transpilaton
$NM = "192.168.66.0/255.255.255.0"
Test-NetConnection 192.168.66.2 -Port 8677          # port smpd osiągalny
mpiexec -timeout 30 -genv MPICH_NETMASK $NM -hosts 3 192.168.66.61 1 192.168.66.2 1 192.168.66.222 1 hostname
mpiexec -timeout 30 -genv MPICH_NETMASK $NM -hosts 3 192.168.66.61 1 192.168.66.2 1 192.168.66.222 1 nvidia-smi -L
mpiexec -timeout 120 -genv MPICH_NETMASK $NM -hosts 3 192.168.66.61 1 192.168.66.2 1 192.168.66.222 1 build\Release\qmap_mpi_tests.exe
```

Pomiar opóźnienia i przepustowości MPI między hostami (`qmap_mpi_bench`, wymaga reguły zapory jak dla `qmap_ga`;
opis w [kampania-iteracja-1.md](kampania-iteracja-1.md)):

```powershell
mpiexec -timeout 120 -genv MPICH_NETMASK $NM -hosts 3 192.168.66.61 1 192.168.66.2 1 192.168.66.222 1 build\Release\qmap_mpi_bench.exe --out results\bench\mpi_pingpong.csv
```

Przykład MPMD z różnym `--pop` na każdej karcie. `--log results\run.csv` daje plik `run_r<rank>.csv` na dysku
**danego** hosta (katalog jest tworzony automatycznie); `--summary`, `--out-qasm`, `--out-layout` zapisuje tylko
rank 0:

```powershell
mpiexec -timeout 120 -genv MPICH_NETMASK $NM -n 1 -host 192.168.66.61 build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 262144 --max-time 20 --generations 100000 --log results\run.csv --summary results\summary.json : -n 1 -host 192.168.66.2 build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 131072 --max-time 20 --generations 100000 --log results\run.csv : -n 1 -host 192.168.66.222 build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 65536 --max-time 20 --generations 100000 --log results\run.csv
```

Diagnostyka zawieszeń: najpierw każdy host osobno (`-hosts 1 <IP> 1 ...`), potem pary, na końcu wszystkie trzy;
pierwszy wariant, który wisi, wskazuje przyczynę. Okno `smpd -d` na danym komputerze pokazuje połączenia i
powody odrzucenia.

| Objaw | Przyczyna / rozwiązanie |
|---|---|
| `connect failed - Odmowa dostępu (errno 5)` | brak konta `hpc` z tym samym hasłem u celu (pkt 3) |
| `Serwer RPC jest niedostępny (errno 1722)` | na tym IP nie działa `smpd` (pkt 5), także na własnym komputerze |
| `Add-LocalGroupMember: Group Administrators was not found` | polski Windows: użyj `-SID S-1-5-32-544` |
| `mpiexec` wisi do `-timeout`, a `hostname` działa | brak `-genv MPICH_NETMASK`, użyte `-env` w MPMD, Wi-Fi włączone na laptopie |
| `cannot write results\...` na zdalnym ranku | stara wersja `qmap_ga` (nie tworzyła katalogów) lub brak praw konta `hpc` do repo: `icacls C:\hpc /grant "hpc:(OI)(CI)M" /T` |

## Problemy

| Objaw | Rozwiązanie |
|---|---|
| `winget` nie istnieje | Zainstaluj „Instalator aplikacji” (App Installer) ze Sklepu Microsoft. |
| `[FAIL] CUDA <-> VS 2022 integration` | CUDA zainstalowano przed VS 2022. `winget uninstall Nvidia.CUDA`, potem ponownie `setup-windows.ps1`. |
| `[FAIL] nvcc 12.9`, a jest inna wersja CUDA | Kilka wersji CUDA może współistnieć; upewnij się, że `CUDA_PATH` wskazuje na `v12.9` (Ustawienia systemu → Zmienne środowiskowe). |
| CMake: `No CUDA toolset found` | To samo co brak integracji CUDA z VS 2022 (wyżej). |
| Polskie znaki „krzaczki” w plikach | Pliki zapisujemy w UTF-8; nie twórz plików przekierowaniem `>` w PowerShellu 5.1. |

Konfiguracja sieci i zapory dla MPI między komputerami: Krok 7 powyżej.
