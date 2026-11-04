# Dziennik iteracji i wyników

Każda iteracja optymalizacji ma ten sam układ: **wersja → profil → wąskie gardło → poprawka → zysk**.
Poprawki wprowadzamy dopiero po zmierzeniu (profilowanie Nsight / pomiar czasu), nie na zapas.

## v1 (naiwna wersja bazowa)

- Wersja (fitness na GPU, `qmap::GpuFitness`): jeden wątek CUDA na osobnika, blok 256 wątków, siatka
  `ceil(pop/256)`. Wszystkie dane w zwykłej pamięci globalnej: populacja (`pop × 27` genów `uint8`), lista par 2q,
  tabele `dist` i `next_hop`, wynik (`int` na osobnika). Lokalne tablice `l2p` / `p2l` (po 32 bajty) w rejestrach/pamięci
  lokalnej wątku; routing to ta sama procedura `qmap::count_swaps` co na CPU. Przy każdym `evaluate()`: synchroniczna
  kopia H2D populacji z pamięci pageable, kernel, `cudaDeviceSynchronize`, synchroniczna kopia D2H wyników. Bez
  `__constant__`, pamięci współdzielonej, strumieni, pamięci przypiętej i grafów CUDA. Zakresy NVTX:
  `fitness_h2d`, `fitness_kernel`, `fitness_d2h`. Pomiary (commit, parametry GA, sprzęt): _do uzupełnienia_.
- Profil: _do uzupełnienia_.
- Wąskie gardło: _do uzupełnienia_.
- Poprawka: brak (v1 jest punktem odniesienia).
- Zysk: brak.

## Kandydaci do optymalizacji

**Żaden z poniższych punktów nie jest zaimplementowany w v1.** To lista pomysłów do sprawdzenia po profilowaniu,
w kolejności wynikającej z wyników, a nie z tej listy.

- Lookahead w routerze (uwzględnienie kilku kolejnych bramek 2q przy wyborze SWAP) – mniej SWAP, droższy fitness.
- Pomijanie kolejnych identycznych par CX (powtórzona para, już sąsiednia, nie wymaga ponownego sprawdzania).
- Lista par `pairs` w pamięci `__constant__` (odczyt jednakowy dla wszystkich wątków – broadcast).
- Tabele `dist` / `next_hop` w pamięci współdzielonej (stagowane raz na blok).
- Pamięć przypięta (pinned) hosta, kopie asynchroniczne i strumienie CUDA – nakładanie transferów i obliczeń.
- Trzymanie populacji na GPU (operatory GA na urządzeniu, bez kopii H2D co generację).
- Dobór rozmiaru bloku / zajętość (occupancy) multiprocesorów.

Każdy z powyższych punktów wymaga pomiaru przed i po zmianie (Nsight Systems `nsys`, Nsight Compute `ncu`),
zanim trafi do wyników; nie zakładamy z góry, że przyniesie zysk.
