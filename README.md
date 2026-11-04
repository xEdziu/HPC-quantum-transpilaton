# Rozproszona optymalizacja mapowania kubitów (GA wyspowy, klaster GPU)

Projekt z przedmiotu **Obliczenia wysokiej wydajności** (Politechnika Wrocławska).

Cel: zminimalizować liczbę bramek SWAP wstawianych przez routing do obwodu kwantowego
na urządzeniu IBM Falcon 27q. Narzędzie: asynchroniczny wyspowy algorytm genetyczny
(C++/CUDA + MPI) uruchamiany na 3 komputerach z Windows 11 w sieci LAN.
Główna metryka: Time-to-Target (TTT); metryka jakości: liczba SWAP względem Qiskit SABRE.

Zespół: Adrian Goral, Paulina Szulc, Mateusz Zubrzycki.

## Struktura repozytorium

```
python/qmap/    pakiet Python: urządzenie, instancje, baseline SABRE, walidacja, eksperymenty
python/tests/   testy pytest
cpp/            GA, router i fitness (C++/CUDA, MS-MPI)
data/devices/   opis urządzenia (JSON), commitowany
data/instances/ obwody kanoniczne (QASM 2.0), commitowane
docs/           dokumentacja i ADR (docs/adr/)
results/        wyniki eksperymentów (ignorowane przez git)
```

## Instalacja (nowy komputer)

Instrukcja krok po kroku: [docs/instalacja-windows.md](docs/instalacja-windows.md).
W skrócie, w PowerShellu uruchomionym jako administrator, w katalogu repo:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\setup-windows.ps1   # VS 2022 Build Tools, CUDA 12.9, MS-MPI, CMake, uv
```

Po restarcie: `.\scripts\check-env.ps1` musi pokazać same `[ OK ]`.

## Python (uv)

Środowisko zarządzane wyłącznie przez [uv](https://docs.astral.sh/uv/)
(`pyproject.toml`, `uv.lock`, `.python-version`):

```
uv sync
uv run pytest
uv run python -m qmap.device
uv run python -m qmap.instances
```

Nie używamy `requirements.txt` ani samego `pip`.

## C++ (CMake)

Wymagany CMake >= 3.24, VS 2022 Build Tools (MSVC) i CUDA 12.9 (architektury sm_86 i sm_120).
Generator trzeba wskazać jawnie (VS 2026 nie jest wspierany przez `nvcc`); nlohmann_json pobierany jest
przez `FetchContent`. Biblioteka `qmap_core` jest tylko hostowa, kod CUDA (`qmap_gpu`) jest osobno.

```
cmake -S cpp -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\qmap_eval.exe --instance data\instances\qft_n8.qasm --device data\devicesalcon27.json
```

Jeśli katalog `build` był skonfigurowany innym generatorem (np. VS 2026), usuń go przed konfiguracją.
Test `qmap_gpu_tests` wymaga karty NVIDIA i porównuje fitness z GPU z routerem CPU.

Test krzyżowy z walidatorem Pythona (`python/tests/test_router_crosscheck.py`) szuka
`build/Release/qmap_eval.exe` albo zmiennej `QMAP_EVAL`; bez pliku jest pomijany.
