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

## GA v1 (jedna wyspa)

Naiwna wersja bazowa algorytmu genetycznego (`qmap::run_ga`, plik wykonywalny `qmap_ga`), bez żadnych optymalizacji:

- Operatory GA działają na hoście, w jednym wątku, w prostych pętlach: inicjalizacja losowymi permutacjami
  (`std::shuffle`), selekcja turniejowa (domyślnie k = 3), krzyżowanie OX (dwa punkty cięcia, jedno dziecko, z
  prawdopodobieństwem 0,9, w przeciwnym razie kopia rodzica 1), mutacja przez zamianę dwóch genów (z
  prawdopodobieństwem 0,3 na dziecko; może przenieść kubit logiczny na wolny kubit fizyczny), elitaryzm (2 najlepsze).
- Cała populacja jest ponownie oceniana na GPU w każdej generacji, łącznie z elitami (bez cache'owania fitnessu).
- Populacja leży na hoście jako `std::vector<uint8_t>` (`pop × 27` genów); w każdej generacji budowany jest nowy wektor
  (zamiana dwóch buforów).
- Wywołania GPU są synchroniczne: operatory hosta i ocena na GPU wykonują się jedno po drugim, bez nakładania.
- Log CSV (`--log`), jeden wiersz na generację (generacja 0 = populacja początkowa), kolumny:
  `generation, elapsed_s, best, mean, gen_time_s, eval_time_s, ops_time_s, evals, evals_per_s`
  (`eval_time_s` – czas wywołania oceny, `ops_time_s` – selekcja, krzyżowanie, mutacja i elitaryzm).
- Zakresy NVTX: `ga_run` (cały przebieg), `ga_evaluate` (wywołanie oceny z GA) oraz zakresy wewnątrz `GpuFitness`:
  `fitness_h2d`, `fitness_kernel`, `fitness_d2h`. Operatory hosta nie mają własnych zakresów.
- Pomiary (commit, parametry GA, sprzęt): _do uzupełnienia_.

## Kandydaci do optymalizacji

**Żaden z poniższych punktów nie jest zaimplementowany w v1.** To lista pomysłów do sprawdzenia po profilowaniu,
w kolejności wynikającej z wyników, a nie z tej listy.

- Lookahead w routerze (uwzględnienie kilku kolejnych bramek 2q przy wyborze SWAP) – mniej SWAP, droższy fitness.
- Pomijanie kolejnych identycznych par CX (powtórzona para, już sąsiednia, nie wymaga ponownego sprawdzania).
- Lista par `pairs` w pamięci `__constant__` (odczyt jednakowy dla wszystkich wątków – broadcast).
- Tabele `dist` / `next_hop` w pamięci współdzielonej (stagowane raz na blok).
- Pamięć przypięta (pinned) hosta, kopie asynchroniczne i strumienie CUDA – nakładanie transferów i obliczeń.
- Trzymanie populacji na GPU (operatory GA na urządzeniu, bez kopii H2D co generację).
- Cache'owanie fitnessu elit (nie oceniamy ponownie osobników skopiowanych bez zmian).
- Wielowątkowe operatory hosta (OpenMP).
- Nakładanie operatorów hosta na ocenę GPU (podwójne buforowanie populacji i strumienie CUDA).
- Drobniejsze zakresy NVTX (selekcja, krzyżowanie, mutacja, elitaryzm), jeśli profil wskaże, że host jest wąskim gardłem.
- Dobór rozmiaru bloku / zajętość (occupancy) multiprocesorów.

Każdy z powyższych punktów wymaga pomiaru przed i po zmianie (Nsight Systems `nsys`, Nsight Compute `ncu`),
zanim trafi do wyników; nie zakładamy z góry, że przyniesie zysk.
