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

> **Do zweryfikowania przez zespół na prawdziwej sieci LAN.** Poniższa procedura jest przygotowana na podstawie
> dokumentacji MS-MPI i nie była jeszcze przetestowana na trzech komputerach.

Na każdym hoście (komputery `HOST_A`, `HOST_B`, `HOST_C`; sieć LAN ustawiona jako **prywatna**):

1. **Ta sama ścieżka do repozytorium i zbudowanego pliku wykonywalnego** na każdym hoście, np.
   `E:\mgr\sem2\HPC-quantum-transpilaton\build\Release\qmap_ga.exe` (ten sam układ katalogów, w tym `data\`).
   Zbuduj projekt (Krok 5) na każdym komputerze osobno albo skopiuj `build\Release` i `data`; wersje muszą być
   identyczne.
2. **Usługa `smpd`.** Albo usługa MS-MPI Launch Service (`msmpilaunchsvc`, instalowana z MS-MPI; sprawdź w
   `services.msc`, że działa), albo ręcznie w osobnym oknie PowerShell na każdym hoście:
   ```powershell
   & "C:\Program Files\Microsoft MPI\Bin\smpd.exe" -d
   ```
3. **Zapora Windows** (PowerShell jako administrator, na każdym hoście): zezwól na `smpd.exe`, `mpiexec.exe` i
   `qmap_ga.exe` w profilu **Prywatnym** (ścieżkę do `qmap_ga.exe` dostosuj):
   ```powershell
   $rules = @{
     "MS-MPI smpd"    = "C:\Program Files\Microsoft MPI\Bin\smpd.exe"
     "MS-MPI mpiexec" = "C:\Program Files\Microsoft MPI\Bin\mpiexec.exe"
     "qmap_ga"        = "E:\mgr\sem2\HPC-quantum-transpilaton\build\Release\qmap_ga.exe"
   }
   foreach ($n in $rules.Keys) {
     New-NetFirewallRule -DisplayName $n -Program $rules[$n] -Direction Inbound -Action Allow -Profile Private
   }
   ```
4. **Użytkownik.** Najprościej to samo konto Windows z tym samym hasłem na wszystkich hostach. Inaczej podaj dane
   jawnie: `mpiexec -pwd <hasło> ...` (lub `-user`, zob. `mpiexec -help2`). Pierwsze uruchomienie z `-pwd` /
   `-savecreds` może poprosić o hasło.

Test łączności (powinien wypisać nazwy obu komputerów):

```powershell
mpiexec -hosts 2 HOST_A 1 HOST_B 1 hostname
```

Przykład MPMD z różnym `--pop` na każdym hoście (liczność populacji dobrana do karty; `{rank}` w `--log` daje osobny
plik na rank, ale zapisywany na dysku danego hosta):

```powershell
mpiexec -n 1 -host HOST_A build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 1000000 --target 20 --max-time 120 --migration adaptiveB --log results\run_r{rank}.csv --out-qasm results\best.qasm --out-layout results\best.json : -n 1 -host HOST_B build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 400000 --target 20 --max-time 120 --migration adaptiveB --log results\run_r{rank}.csv : -n 1 -host HOST_C build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 200000 --target 20 --max-time 120 --migration adaptiveB --log results\run_r{rank}.csv
```

Uruchamiaj z katalogu repo (ścieżki względne `build\...`, `data\...` muszą istnieć u każdego hosta w tym samym
katalogu roboczym; `mpiexec` przekazuje katalog bieżący). Wyniki (`--out-qasm`, `--out-layout`) zapisuje wyłącznie
rank 0. Wartość `--target 20` jest tylko przykładem; poziomy celu opisuje [ADR 0010](adr/0010-ttt.md).

Typowe problemy: `Failed to connect to smpd` (usługa nie działa albo zapora blokuje), błąd logowania (inne
konto/hasło, użyj `-pwd`), `The system cannot find the file specified` (inna ścieżka do `qmap_ga.exe` na hoście).

## Problemy

| Objaw | Rozwiązanie |
|---|---|
| `winget` nie istnieje | Zainstaluj „Instalator aplikacji” (App Installer) ze Sklepu Microsoft. |
| `[FAIL] CUDA <-> VS 2022 integration` | CUDA zainstalowano przed VS 2022. `winget uninstall Nvidia.CUDA`, potem ponownie `setup-windows.ps1`. |
| `[FAIL] nvcc 12.9`, a jest inna wersja CUDA | Kilka wersji CUDA może współistnieć; upewnij się, że `CUDA_PATH` wskazuje na `v12.9` (Ustawienia systemu → Zmienne środowiskowe). |
| CMake: `No CUDA toolset found` | To samo co brak integracji CUDA z VS 2022 (wyżej). |
| Polskie znaki „krzaczki” w plikach | Pliki zapisujemy w UTF-8; nie twórz plików przekierowaniem `>` w PowerShellu 5.1. |

Konfiguracja sieci i zapory dla MPI między komputerami: Krok 7 powyżej.
