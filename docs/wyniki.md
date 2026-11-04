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

## MPI v1 (wyspy, migracja asynchroniczna)

Naiwna wersja bazowa warstwy MPI (`qmap_mpi`, `qmap_ga` zawsze pod MPI; bez `mpiexec` działa jako pojedyncza wyspa).
Decyzje: [0008](adr/0008-migracja.md), [0009](adr/0009-zakonczenie.md), [0010](adr/0010-ttt.md).

- Jeden rank = jedna wyspa = jedna karta GPU (urządzenie 0 hosta), własny `--pop` na rank (MPMD); ziarno rankingu
  `splitmix64(seed ^ (0x9E3779B97F4A7C15 · (rank + 1)))`.
- Migracja: raz na generację (w haku `run_ga`, po ocenie): `MPI_Test` po wszystkich oczekujących wysyłkach (zakończone
  usuwane), `MPI_Iprobe` + `MPI_Recv` wszystkiego, co dotarło, przybysz nadpisuje najgorszego osobnika. Wysyłka:
  `MPI_Isend` z **nowym buforem na stercie dla każdego adresata**; jeden osobnik (najlepszy dotychczasowy) na migrację.
  Topologia `all` albo `ring`; interwał `fixed:N`, `adaptiveA`, `adaptiveB` (w generacjach).
- Zakończenie: `TAG_STOP` od rangi, która osiągnęła cel, potem `MPI_Alltoall` liczników, odebranie reszty, `MPI_Waitall`
  i redukcje wyników (bez `MPI_Cancel`).
- Czas: `MPI_Barrier` tuż przed `run_ga`, TTT = minimum po rangach lokalnych czasów od wyjścia z bariery.
- Log CSV na rank (`--log` z `{rank}`): kolumny jak w GA v1 oraz `migrants_sent, migrants_received, interval`
  (skumulowane liczby komunikatów, bieżące τ).
- Zakresy NVTX: `mpi_migrate` (praca wysyłająca i odbierająca w generacji) i `mpi_terminate` (protokół zakończenia).
- Pomiary (commit, parametry, sprzęt, TTT dla ε ∈ {0,10, 0,05, 0,02}): _do uzupełnienia_.

Kandydaci specyficzni dla MPI (nie zaimplementowani; do sprawdzenia po profilowaniu):

- Trwałe żądania (`MPI_Send_init`) lub pula buforów zamiast nowego bufora sterty na każdy `MPI_Isend`.
- Migracja kilku osobników naraz (np. najlepsi + losowi) w jednym komunikacie.
- Nakładanie migracji na ocenę GPU (komunikacja podczas kernela; wymaga podwójnego buforowania i strumieni).
- Interwał migracji oparty na czasie (sekundy), nie na generacjach, aby wyspy o różnej prędkości migrowały równie
  często w czasie rzeczywistym.

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
