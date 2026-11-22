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

### Obserwacje wstępne (nie pomiar)

Z jednego testu dymnego (`qft_n12`, `--pop 4096`, RTX 5080, 2 rangi współdzielące jedną kartę, logi CSV); nie są
wynikiem eksperymentu i **wymagają potwierdzenia profilem** ([profilowanie.md](profilowanie.md)):

- **Wydajność:** średnio na generację czas oceny (`eval_time_s`) ≈ 0,35 ms, a operatory GA na hoście (`ops_time_s`)
  ≈ 0,68 ms. Przy małej populacji dominują operatory hosta, nie GPU. Przed jakąkolwiek zmianą: `nsys` (zakresy
  NVTX operatorów trzeba dodać) oraz sprawdzenie przy większym `--pop`.
- **Jakość:** dla `qft_n12` GA v1 osiąga najlepszy wynik 150 SWAP po ok. 1 s (ok. 1000 generacji), po czym
  stagnuje; SABRE (minimum po ziarnach) daje 51 SWAP. Wniosek wstępny: zachłanny router v1 jest naiwny, a populacja
  zbiega przedwcześnie (utrata różnorodności). Kandydaci: lookahead w routerze (poniżej) oraz zachowanie różnorodności /
  restart przy stagnacji.

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
- Log CSV na rank (`--log results\run.csv` → `run_r<rank>.csv`): kolumny jak w GA v1 oraz `migrants_sent, migrants_received, interval`
  (skumulowane liczby komunikatów, bieżące τ).
- Zakresy NVTX: `mpi_migrate` (praca wysyłająca i odbierająca w generacji) i `mpi_terminate` (protokół zakończenia).
- Pomiary (commit, parametry, sprzęt, TTT dla ε ∈ {0,10, 0,05, 0,02}): _do uzupełnienia_.

Kandydaci specyficzni dla MPI (nie zaimplementowani; do sprawdzenia po profilowaniu):

- Trwałe żądania (`MPI_Send_init`) lub pula buforów zamiast nowego bufora sterty na każdy `MPI_Isend`.
- Migracja kilku osobników naraz (np. najlepsi + losowi) w jednym komunikacie.
- Nakładanie migracji na ocenę GPU (komunikacja podczas kernela; wymaga podwójnego buforowania i strumieni).
- Interwał migracji oparty na czasie (sekundy), nie na generacjach, aby wyspy o różnej prędkości migrowały równie
  często w czasie rzeczywistym.

## Iteracja I – pomiary węzłów (V0, 2026-11-22)

Źródło: `scripts/bench-node.ps1` (commit `19fc6b2`), instancja `qft_n12`, 30 generacji na rozmiar populacji, jeden rank
na węzeł; pliki `results/bench/bench_<host>_sweep.csv`, `bench_<host>_pop<P>_r0.csv`, `nvsmi_<host>.csv`; wykresy
`results/plots/bench_sweep.png`, `bench_nvsmi_<host>.png`. Na 5080 przemiatanie powtórzono dwukrotnie: różnice ≤ 3 %.

Przepustowość całej generacji (oceny/s = populacja × generacje / czas):

| Populacja | RTX 5080 | RTX 5070 Laptop | RTX 3060 |
|---|---|---|---|
| 4 096 | 4,69 mln | 3,59 mln | 2,62 mln |
| 16 384 | 6,03 mln | 4,69 mln | 2,87 mln |
| 65 536 | 6,22 mln | 4,30 mln | 2,86 mln |
| 262 144 | 5,93 mln | 4,34 mln | 2,73 mln |
| 1 048 576 | 4,17 mln | 2,85 mln | 1,96 mln |
| 4 194 304 | 3,14 mln | 1,98 mln | 1,56 mln |

Podział czasu generacji przy populacji 262 144 (średnie z logu CSV):

| Węzeł | Generacja | Ocena na GPU (H2D + kernel + D2H) | Operatory GA na hoście | Udział hosta | Przepustowość samej oceny GPU |
|---|---|---|---|---|---|
| RTX 5080 (Ryzen 9 9950X3D) | 43,4 ms | 3,5 ms | 39,9 ms | 92 % | 74 mln/s |
| RTX 5070 Laptop (i7-14650HX) | 59,6 ms | 6,5 ms | 53,1 ms | 89 % | 40 mln/s |
| RTX 3060 (i5-10400) | 93,1 ms | 12,1 ms | 81,0 ms | 87 % | 22 mln/s |

Przebiegi ciągłe 120 s (log `nvidia-smi` co 500 ms w oknie przebiegu):

| Węzeł (populacja) | Oceny/s | Śr. wykorzystanie GPU | Śr. zegar SM | Śr. moc | Maks. temp. |
|---|---|---|---|---|---|
| RTX 5080 (262 144) | 5,57 mln | 11 % | 818 MHz (maks. 3090) | 27,5 W (limit 360) | 39 °C |
| RTX 5070 Laptop (65 536) | 3,31 mln | 13 % | 614 MHz (maks. 3090) | 18,4 W | 53 °C |
| RTX 3060 (131 072) | 2,21 mln | 34 % | 775 MHz (maks. 2130) | 32,5 W (limit 170) | 44 °C |

Wnioski z pomiarów (diagnoza V0, przed jakąkolwiek zmianą kodu):

1. **Wąskie gardło: jednowątkowe operatory GA na hoście.** Na każdej karcie i przy każdej populacji ≥ 16 384 zajmują
   87–94 % czasu generacji; GPU ocenia populację 8–13× szybciej, niż host ją tworzy. Dowody: kolumny `ops_time_s` i
   `eval_time_s` w logach, niski udział wykorzystania GPU w `nvidia-smi`. Do potwierdzenia osią czasu `nsys`
   (krok 7 kampanii).
2. **Spadek przepustowości powyżej ok. 260 tys. osobników** na wszystkich kartach. Hipoteza: populacja (27 B na
   osobnika, dwa bufory) przestaje mieścić się w pamięci podręcznej CPU, a losowy dostęp turnieju i krzyżowania staje się
   ograniczony pamięcią – do sprawdzenia profilem CPU.
3. **Skutek uboczny: GPU zbija zegary.** Karta bezczynna przez ok. 90 % czasu przechodzi w niskie stany P (P5/P8) i
   pracuje na 20–40 % maksymalnego zegara; w przebiegu ciągłym czas samej oceny jest przez to dłuższy niż w krótkim
   przemiataniu (laptop: 3,8 ms zamiast 1,9 ms przy 65 536; 5080: 5,7 ms zamiast 3,5 ms przy 262 144). Temperatury są
   niskie (≤ 53 °C), więc **nie jest to dławienie termiczne**. Bit `0x4` (SW power cap) pojawił się tylko 3 razy na
   laptopie; znaczenie bitu `0x400` w `clocks_event_reasons.active` do sprawdzenia w `nvidia-smi -q -d PERFORMANCE`.
4. **Heterogeniczność jest ograniczona przez CPU, nie przez GPU.** Stosunek samej oceny GPU 5080 : 5070 Laptop : 3060
   wynosi ok. 1 : 0,54 : 0,29, a stosunek całych generacji ok. 1 : 0,73 : 0,47 – słabszy CPU (i5-10400) ogranicza
   3060 bardziej niż jego GPU.

## Iteracja I – koszt komunikacji MPI (2026-11-22)

`qmap_mpi_bench` (rank 0 = AMG-NORTH, ping-pong z każdym rankiem, połowa czasu pętli), plik
`results/bench/mpi_pingpong.csv`:

| Para | 1 B (mediana / min) | 32 KiB | 1 MiB – przepustowość |
|---|---|---|---|
| AMG-NORTH ↔ PS04 (karty PCIe, 1 GbE) | 108 µs / 88 µs | 425 µs | 102 MB/s |
| AMG-NORTH ↔ HyperX_Zuber (karta USB, 1 GbE, laptop w użyciu) | 398 µs / 191 µs | 1174 µs | 37 MB/s |
| `MPI_Barrier`, 3 rangi | 795 µs / 241 µs | – | – |

Wnioski:
- Komunikat migracji (31 B) kosztuje 0,1–0,4 ms, czyli **< 1 %** czasu jednej generacji (43–93 ms) i jest wysyłany
  co 100 generacji – w V0 komunikacja **nie** jest wąskim gardłem.
- Łącze do PS04 osiąga ok. 102 MB/s ≈ 0,8 Gb/s, czyli praktyczny limit 1 GbE. Łącze do laptopa jest ok. 4× wolniejsze
  i ma wyższe opóźnienie (karta sieciowa na USB, laptop w normalnym użyciu) – istotne dopiero przy większych
  komunikatach (np. migracja wielu osobników).
- Do powtórzenia po kampanii TTT (porównanie warunków).

## Iteracja I – pilotaż i progi TTT (2026-11-22)

Konfiguracja: wszystkie 3 węzły (5080 / 5070 Laptop / 3060, populacje 262 144 / 65 536 / 131 072), 4 instancje,
3 ziarna, 300 s na przebieg, bez celu, migracja `adaptiveB`, topologia `all`. Każdy przebieg zakończył się
`stop=max_time` (ok. 300 s). Wcześniejszy pilotaż z domyślnym limitem 1000 generacji jest w `results/pilot_gen1000`
(odrzucony, zob. dziennik decyzji).

Najlepsza znana wartość B (minimum z 3 ziaren) i progi T_ε = ⌈(1+ε)·B⌉ (`results/pilot/targets.csv`):

| Instancja | Wyniki 3 ziaren | B | T_0,10 | T_0,05 | T_0,02 | SABRE min / mediana |
|---|---|---|---|---|---|---|
| qft_n12 | 150 / 151 / 152 | 150 | 165 | 158 | 153 | 51 / 53 |
| qft_n20 | 698 / 654 / 666 | 654 | 720 | 687 | 668 | 170 / 177 |
| qaoa_n16 | 30 / 27 / 28 | 27 | 30 | 29 | 28 | 19 / 20 |
| qaoa_n24 | 51 / 58 / 52 | 51 | 57 | 54 | 53 | 35 / 41 |

Względem pilotażu z limitem 1000 generacji (ok. 60 s) B poprawiło się tylko dla qft_n20 (686 → 654) i qaoa_n24
(53 → 51).

Czas do osiągnięcia progu w przebiegach pilotażowych (minimum po rankach, z logów `run_r*.csv`; „–” = nie osiągnięto
w 300 s):

| Instancja | Ziarno | T_0,10 | T_0,05 | T_0,02 |
|---|---|---|---|---|
| qft_n12 | 1 | 2,1 s | 4,0 s | 14,6 s |
| qft_n12 | 2 | 1,1 s | 9,3 s | 47,1 s |
| qft_n12 | 3 | 1,5 s | 2,6 s | 105,0 s |
| qft_n20 | 1 | 7,1 s | – | – |
| qft_n20 | 2 | 1,6 s | 16,8 s | 135,4 s |
| qft_n20 | 3 | 1,7 s | 44,5 s | 96,1 s |
| qaoa_n16 | 1 | 84,1 s | – | – |
| qaoa_n16 | 2 | 45,7 s | 53,7 s | 285,2 s |
| qaoa_n16 | 3 | 9,4 s | 18,4 s | 26,7 s |
| qaoa_n24 | 1 | 19,5 s | 19,5 s | 28,8 s |
| qaoa_n24 | 2 | – | – | – |
| qaoa_n24 | 3 | 19,1 s | 43,9 s | 45,0 s |

Liczba przebiegów (z 12), które osiągnęły próg w danym czasie, konfiguracja 3-hostowa:

| Limit czasu | T_0,10 | T_0,05 | T_0,02 |
|---|---|---|---|
| 60 s | 10 | 8 | 5 |
| 120 s | 11 | 8 | 7 |
| 300 s | 11 | 8 | 9 |

Przepustowość w klastrze (qft_n12, ziarno 1, oceny / 300 s): 5080 ok. 5,55 mln/s (6351 generacji, ok. 47 ms na
generację), 5070 Laptop ok. 3,30 mln/s (15 090 generacji, ok. 20 ms), 3060 ok. 2,16 mln/s (4954 generacje, ok. 61 ms);
razem ok. 11,0 mln/s. Wartości dla laptopa i 3060 są niższe niż w pomiarze pojedynczego węzła (`bench-node`);
przyczyna nie jest zbadana (laptop był w tym czasie używany, zob. zagrożenia trafności).

Obserwacje:
- **Rozkład TTT jest dwumodalny.** Przebieg albo osiąga próg w kilkadziesiąt sekund, albo utyka na jednej wartości
  do końca (qaoa_n24, ziarno 2: 58 przez 300 s, nie osiągnął nawet T_0,10; qft_n20, ziarno 1: 698). Wydłużenie
  przebiegu powyżej ok. 2 minut daje niewiele (T_0,02: 5 → 7 → 9 z 12 przy 60 / 120 / 300 s).
- Na końcu przebiegu wszystkie wyspy mają zwykle tę samą wartość best: migracja `all` szybko ujednolica populacje
  (wyjątek: qaoa_n16, ziarno 2 – 27 na 5080, 29 na pozostałych).
- To ten sam objaw przedwczesnej zbieżności co w pilotażu z limitem generacji. Jest to słaby punkt algorytmiczny
  (kandydat: restart / zachowanie różnorodności), niezależny od wąskiego gardła na CPU hosta.
- Luka jakości względem SABRE (QFT ok. 3–4×, QAOA ok. 1,3–1,5×) wynika z naiwnego routera zachłannego (ADR 0007),
  nie z czasu przebiegu.

## Kandydaci do optymalizacji

**Żaden z poniższych punktów nie jest zaimplementowany w v1.** To lista pomysłów do sprawdzenia po profilowaniu,
w kolejności wynikającej z wyników, a nie z tej listy.

- Lookahead w routerze (uwzględnienie kilku kolejnych bramek 2q przy wyborze SWAP) – mniej SWAP, droższy fitness.
- Zachowanie różnorodności populacji i restart przy stagnacji (np. restart wyspy po N generacjach bez poprawy,
  niszowanie, większa mutacja przy stagnacji) – przeciw przedwczesnej zbieżności.
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
