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

Na razie tylko host (C++17, bez CUDA); wymagany CMake >= 3.24 i kompilator MSVC. Generator Visual Studio
jest wykrywany automatycznie, nlohmann_json pobierany przez `FetchContent`.

```
cmake -S cpp -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\qmap_eval.exe --instance data\instances\qft_n8.qasm --device data\devices\falcon27.json
```

Test krzyżowy z walidatorem Pythona (`python/tests/test_router_crosscheck.py`) szuka
`build/Release/qmap_eval.exe` albo zmiennej `QMAP_EVAL`; bez pliku jest pomijany.
