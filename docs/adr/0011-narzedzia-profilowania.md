# 0011. Narzędzia profilowania: Nsight Systems, Nsight Compute i NVTX

- Status: Zaakceptowana
- Data: 2026-11-04

## Kontekst

Optymalizacje (lista kandydatów w [wyniki.md](../wyniki.md)) mają być wprowadzane dopiero po pomiarze. Potrzebujemy
narzędzi, które działają na natywnym Windows 11 z CUDA 12.9 ([0001](0001-windows-ms-mpi.md)), na kartach sm_86
(RTX 3060) i sm_120 (RTX 5080, RTX 5070 Laptop), oraz pozwalają zobaczyć zarówno oś czasu programu (kopie, kernele,
host, MPI), jak i wnętrze kernela fitness.

## Rozważane opcje

- NVVP (Visual Profiler) i `nvprof`.
- Nsight Systems + Nsight Compute, z zakresami NVTX w kodzie.
- Śledzenie MPI w `nsys` (`--trace=mpi`) albo narzędzia MPI innych firm (Intel Trace Analyzer, Score-P, Vampir).
- Wyłącznie własne pomiary czasu w kodzie (log CSV).

## Decyzja

- **Nsight Systems** (`nsys`) do osi czasu i **Nsight Compute** (`ncu`) do analizy kernela fitness, zgodnie z
  procedurą w [profilowanie.md](../profilowanie.md). Oba korzystają z CUPTI.
- **Zakresy NVTX** (`nvtx3`): `ga_run`, `ga_evaluate`, `fitness_h2d`, `fitness_kernel`, `fitness_d2h`,
  `mpi_migrate`, `mpi_terminate`. Biblioteka `qmap_mpi` pozostaje wolna od CUDA: zakresy wstrzykuje `qmap_ga`
  przez wywołania zwrotne `Island`.
- **Komunikację MPI oceniamy zakresami NVTX**, nie śledzeniem MPI w `nsys`: na Windows z MS-MPI nie zakładamy
  wsparcia `nsys --trace=mpi` (opcja nie jest dostępna dla MS-MPI na Windows; do zweryfikowania), a zakresy
  `mpi_migrate` i `mpi_terminate` pokazują koszt widziany z hosta w tym samym raporcie co kernele. Profil `nsys` robimy
  na każdym hoście osobno.
- Kernele są kompilowane z `--generate-line-info`, co wystarcza do widoku źródła w `ncu` bez wersji debug.
- Nie używamy NVVP ani `nvprof`.
- Log CSV GA (`ops_time_s`, `eval_time_s`, `evals_per_s`) zostaje pierwszym, tanim źródłem wskazówek; wnioski o wąskim
  gardle wymagają potwierdzenia profilem.

## Argumenty za i przeciw

### Nsight Systems + Nsight Compute + NVTX (wybrane)
- Za: aktualne, wspierane dla sm_86 i sm_120, instalowane z CUDA 12.9; `nsys` odpowiada, gdzie jest czas, a `ncu`,
  dlaczego kernel jest wolny; NVTX nie kosztuje przy braku profilera.
- Przeciw: `ncu` na kartach GeForce wymaga włączenia dostępu do liczników GPU ([instalacja-windows.md](../instalacja-windows.md),
  Krok 6); pełny zestaw sekcji `ncu` spowalnia kernel wielokrotnie (profilujemy kilka wywołań).

### NVVP i nvprof
- Za: znane z podręczników.
- Przeciw: nie wspierają compute capability ≥ 7.5 (nasze karty: sm_86, sm_120), a NVVP usunięto z CUDA 13.
  Nie nadają się do tego sprzętu.

### Śledzenie MPI w nsys / narzędzia zewnętrzne
- Za: pełny widok komunikatów.
- Przeciw: brak pewnego wsparcia dla MS-MPI na Windows; dodatkowa instalacja na trzech komputerach; nasz protokół jest
  mały (jedna wiadomość migranta, stop), więc zakresy NVTX wystarczą.

### Tylko pomiary czasu w kodzie
- Za: zero zależności.
- Przeciw: nie pokazuje wnętrza kernela ani czasu CUDA API; łatwo mierzyć nie to, co trzeba.

## Konsekwencje

- Pozytywne: jednolity, powtarzalny sposób pomiaru przed i po każdej optymalizacji; zakresy NVTX są już w kodzie.
- Negatywne / ryzyka: operatory hosta nie mają jeszcze własnych zakresów (dodajemy je dopiero, jeśli profil wskaże
  host jako wąskie gardło); komendy `nsys` z `%q{PMI_RANK}` i obsługę MS-MPI trzeba zweryfikować na sprzęcie;
  profile jednej karty współdzielonej przez kilka rang nie oddają przepustowości klastra.
