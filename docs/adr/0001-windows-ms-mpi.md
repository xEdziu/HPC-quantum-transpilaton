# 0001. Natywny Windows 11 + MS-MPI

- Status: Zaakceptowana
- Data: 2026-11-03

## Kontekst

Wszystkie 3 komputery zespołu (RTX 5080, RTX 5070 Laptop, RTX 3060) działają pod Windows 11
i są połączone siecią LAN. Potrzebujemy implementacji MPI do asynchronicznej migracji
osobników między wyspami GA.

## Rozważane opcje

- Natywny Windows 11 + MS-MPI.
- Natywny Windows 11 + OpenMPI.
- WSL2 + OpenMPI.

## Decyzja

Natywny Windows 11 + **MS-MPI** (runtime + SDK), `mpiexec` i usługa `smpd`.

## Argumenty za i przeciw

### MS-MPI (wybrana)
- Za: wspierana, natywna implementacja dla Windows; integracja z MSVC i CMake (`find_package(MPI)`).
- Za: implementuje MPI 2.2 oraz podzbiór 3.1, co wystarcza (`Isend`, `Iprobe`, `Recv`, `Testsome`, `Alltoall`, `Reduce`).
- Za: ta sama ścieżka do pliku wykonywalnego na wszystkich hostach, uruchamianie przez `smpd`.
- Przeciw: śledzenie MPI w `nsys` jest nastawione na Linuksa; zastępujemy je zakresami NVTX.
- Przeciw: brak nowszych funkcji MPI 3.x/4.x; nie są nam potrzebne.

### OpenMPI natywnie na Windows
- Za: powszechnie używane.
- Przeciw: brak wspieranego natywnego builda dla Windows.

### WSL2 + OpenMPI
- Za: pełny ekosystem Linux, lepsze wsparcie nsys dla MPI.
- Przeciw: odrzucone przez zespół; dodatkowa warstwa sieciowa (NAT) między hostami i komplikacje z dostępem do GPU.

## Konsekwencje

- Kod MPI ograniczamy do funkcji dostępnych w MS-MPI.
- Profilowanie per rank: `nsys` z zakresami NVTX, bez śledzenia MPI.
- Plik wykonywalny musi leżeć pod tą samą ścieżką na każdym hoście; na hostach działa `smpd`, firewall musi to dopuszczać.
