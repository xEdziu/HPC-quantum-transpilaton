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

### Powtórzenie po zmianie karty sieciowej laptopa (2026-11-23)

Przed drugą sesją kampanii TTT laptop przepięto z karty USB na wbudowaną kartę Ethernet (Realtek Gaming GbE,
nowy adres 192.168.66.222; [dziennik decyzji](dziennik-decyzji.md)). Plik `results/bench/mpi_pingpong_2026-11-23.csv`:

| Para | 1 B (mediana / min) | 32 KiB | 1 MiB – przepustowość |
|---|---|---|---|
| AMG-NORTH ↔ PS04 (bez zmian) | 105 µs / 81 µs | 427 µs | 102 MB/s |
| AMG-NORTH ↔ HyperX_Zuber (wbudowana karta, 1 GbE) | 100 µs / 86 µs | 352 µs | 113 MB/s |
| `MPI_Barrier`, 3 rangi | 230 µs / 178 µs | – | – |

Wnioski:
- Łącze do PS04 jest powtarzalne (108 → 105 µs, 102 MB/s), więc pomiar jest stabilny.
- Łącze do laptopa: opóźnienie 398 → 100 µs (ok. 4×), przepustowość 37 → 113 MB/s (ok. 3×), `MPI_Barrier`
  795 → 230 µs. Laptop jest teraz w sieci równorzędny z PS04; za wcześniejszy gorszy wynik odpowiadała karta USB.
- Wpływ na kampanię: komunikacja w V0 i tak zajmowała < 1 % czasu generacji, więc zmiana karty między sesjami
  (powtórzenia 1–4 na karcie USB, 5–10 na wbudowanej) nie powinna wyraźnie zmienić TTT konfiguracji z laptopem.
  Mimo to opisujemy ją w sprawozdaniu jako zagrożenie trafności i porównujemy TTT obu sesji w konfiguracjach z
  laptopem.

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

## Iteracja I – kampania TTT V0 (2026-11-22 – 2026-11-24)

Konfiguracja: 4 konfiguracje węzłów × 4 instancje × 10 powtórzeń = 160 przebiegów po 120 s, migracja `adaptiveB`,
topologia `all`, populacje jak w pilotażu (5080 / 5070 Laptop / 3060: 262 144 / 65 536 / 131 072), progi T_ε z
pilotażu (wszystkie trzy progi w jednym przebiegu). To samo ziarno dla danego powtórzenia we wszystkich konfiguracjach
(porównanie sparowane). Wszystkie 160 przebiegów ma `summary.json` i przeszło `validate-runs` (CheckMap, test
strukturalny, statevector, k = 1). Jeden przebieg (`rtx5080+rtx5070/qaoa_n16/rep5`) powtórzono po błędzie
uruchomienia (`mpiexec` nie połączył się z lokalnym `smpd`), zob. dziennik decyzji. Kampania odbyła się w dwóch sesjach
(powtórzenia 1–4: 2026-11-22, 5–10: 2026-11-23). Źródła: `results/runs`, `results/aggregate.csv`, `results/quality.csv`,
wykresy `results/plots/ttt_ecdf_<instancja>_<eps>.png` i `convergence_runs_rtx5080+rtx5070+rtx3060_qft_n20_rep1.png`.

Mediana TTT jest dolną medianą z cenzurowaniem (porażki = +∞), więc „–” oznacza mniej niż 5 sukcesów na 10.
Komórka: liczba sukcesów w 120 s; mediana TTT [s].

| Instancja | ε | T | rtx5080 | rtx5080+rtx3060 | rtx5080+rtx5070 | rtx5080+rtx5070+rtx3060 |
|---|---|---|---|---|---|---|
| qaoa_n16 | 0,02 | 28 | 7/10; 63,3 | 5/10; 118,9 | 2/10; – | 3/10; – |
| qaoa_n16 | 0,05 | 29 | 8/10; 58,7 | 5/10; 63,3 | 3/10; – | 4/10; – |
| qaoa_n16 | 0,10 | 30 | 8/10; 51,6 | 7/10; 58,5 | 4/10; – | 6/10; 13,3 |
| qaoa_n24 | 0,02 | 53 | 3/10; – | 2/10; – | 4/10; – | 2/10; – |
| qaoa_n24 | 0,05 | 54 | 4/10; – | 2/10; – | 5/10; 102,3 | 5/10; 102,3 |
| qaoa_n24 | 0,10 | 57 | 7/10; 67,0 | 9/10; 46,8 | 8/10; 43,4 | 7/10; 24,1 |
| qft_n12 | 0,02 | 153 | 8/10; 35,4 | 8/10; 14,8 | 9/10; 20,0 | 7/10; 22,9 |
| qft_n12 | 0,05 | 158 | 9/10; 14,6 | 10/10; 4,4 | 10/10; 2,9 | 10/10; 5,1 |
| qft_n12 | 0,10 | 165 | 10/10; 1,6 | 10/10; 1,0 | 10/10; 0,7 | 10/10; 0,7 |
| qft_n20 | 0,02 | 668 | 0/10; – | 2/10; – | 0/10; – | 1/10; – |
| qft_n20 | 0,05 | 687 | 5/10; 94,4 | 6/10; 73,3 | 5/10; 73,6 | 9/10; 28,4 |
| qft_n20 | 0,10 | 720 | 10/10; 6,1 | 10/10; 8,1 | 10/10; 2,3 | 10/10; 2,5 |

Przyspieszenie S względem samego RTX 5080 (mediana do mediany) i w nawiasie efektywność heterogeniczna
E = S / (przepustowość konfiguracji / przepustowość 5080); „–”, gdy którakolwiek mediana jest nieokreślona:

| Instancja | ε | rtx5080+rtx3060 | rtx5080+rtx5070 | rtx5080+rtx5070+rtx3060 |
|---|---|---|---|---|
| qaoa_n16 | 0,02 | 0,53 (0,38) | – | – |
| qaoa_n16 | 0,05 | 0,93 (0,65) | – | – |
| qaoa_n16 | 0,10 | 0,88 (0,62) | – | 3,88 (1,84) |
| qaoa_n24 | 0,02 | – | – | – |
| qaoa_n24 | 0,05 | – | – | – |
| qaoa_n24 | 0,10 | 1,43 (1,02) | 1,54 (0,93) | 2,78 (1,31) |
| qft_n12 | 0,02 | 2,38 (1,63) | 1,76 (1,01) | 1,54 (0,70) |
| qft_n12 | 0,05 | 3,32 (2,27) | 4,99 (2,86) | 2,86 (1,31) |
| qft_n12 | 0,10 | 1,71 (1,17) | 2,35 (1,34) | 2,37 (1,08) |
| qft_n20 | 0,02 | – | – | – |
| qft_n20 | 0,05 | 1,29 (0,91) | 1,28 (0,77) | 3,33 (1,58) |
| qft_n20 | 0,10 | 0,76 (0,53) | 2,60 (1,57) | 2,48 (1,18) |

Przepustowość całej konfiguracji (suma po rankach średnich `evals_per_s`, zakres po instancjach): 5080 4,9–5,9 mln/s,
5080+3060 7,0–8,4 mln/s, 5080+5070 8,1–10,0 mln/s, wszystkie trzy 10,3–12,5 mln/s, czyli ok. 1,4 / 1,7 / 2,1 ×
przepustowości samego 5080.

Przepustowość na rank (mediana `evals_per_s` z `run_r*.csv`) w obu sesjach:

| Węzeł | Sesja 1 (rep 1–4) | Sesja 2 (rep 5–10) |
|---|---|---|
| RTX 5080 | 5,68 mln/s | 5,67 mln/s |
| RTX 3060 | 2,28 mln/s | 2,22 mln/s |
| RTX 5070 Laptop | 3,06 mln/s | 4,61 mln/s |

Jakość (najlepszy wynik GA w całej kampanii wobec SABRE z `results/baseline/sabre.csv`):

| Instancja | GA (min) | SABRE min / mediana | GA / SABRE min |
|---|---|---|---|
| qaoa_n16 | 26 | 19 / 20 | 1,37 |
| qaoa_n24 | 51 | 35 / 41 | 1,46 |
| qft_n12 | 150 | 51 / 53 | 2,94 |
| qft_n20 | 668 | 170 / 177 | 3,93 |

Obserwacje:
- **Trzy węzły skracają TTT 2,4–3,9× przy progach ε = 0,10 i 0,05** (tam, gdzie mediany są określone), przy ok. 2,1×
  większej przepustowości. Dla qft_n20 przy ε = 0,05 rośnie też częstość sukcesu (5/10 → 9/10). Efektywność > 1
  (do 1,8) nie oznacza, że jedna ocena jest tańsza: model wyspowy przeszukuje inaczej (kilka niezależnych populacji
  i migracja), więc TTT nie jest proporcjonalne do liczby ocen. Przy n = 10 i dwumodalnym rozkładzie TTT
  pojedyncze wartości S są bardzo niepewne – wyższy kwartyl jest często nieokreślony (`aggregate.csv`).
- **Próg ε = 0,02 jest dla V0 za ostry:** mediana jest określona tylko dla qft_n12 i qaoa_n16, a qft_n20 osiąga go
  0–2 razy na 10 w każdej konfiguracji. Dla tego progu właściwą miarą jest częstość sukcesu; żadna konfiguracja nie
  poprawia jej systematycznie.
- **Dołożenie węzła nie zawsze pomaga.** 5080+3060 jest wolniejsze od samego 5080 dla qaoa_n16 (S = 0,53–0,93)
  i qft_n20 przy ε = 0,10 (S = 0,76). 5080+5070 dla qaoa_n16 osiąga próg rzadziej niż sam 5080 (2–4 wobec 7–8 na 10).
  Hipoteza (niesprawdzona): wyspa laptopa ma najmniejszą populację i wykonuje ok. 3× więcej generacji niż 5080
  (np. 8000 wobec 2600 w 120 s), a migracja `all` rozsyła jej najlepszego osobnika do pozostałych wysp, co przyspiesza
  przedwczesną zbieżność obserwowaną już w pilotażu. Do sprawdzenia: porównanie topologii `ring` / interwału migracji
  albo równych populacji.
- **Laptop liczył w sesji 2 ok. 50 % szybciej niż w sesji 1** (3,06 → 4,61 mln/s), a 5080 i 3060 tak samo w obu
  sesjach. Sesja 1 zgadza się z pilotażem i przebiegiem 120 s z pomiarów węzłów (ok. 3,3 mln/s; laptop był wtedy
  używany), sesja 2 – z przemiataniem `bench-node` (4,3–4,7 mln/s). Ponieważ ok. 89 % czasu generacji to operatory GA
  na CPU hosta, różnica wynika najpewniej z ustawień zasilania laptopa (zasilacz, tryb wydajności), a nie z sieci.
  Logów `nvidia-smi` z kampanii nie zebrano (plik z 2026-11-22 obejmuje tylko 1 minutę testu), więc nie da się tego
  potwierdzić. Kampanii nie powtarzamy (zob. dziennik decyzji); przed kolejnymi pomiarami obowiązuje lista kontrolna
  z `kampania-iteracja-1.md`.
- **Luka jakości względem SABRE się nie zmienia** (QAOA 1,4–1,5×, QFT 2,9–3,9×), bo wynika z naiwnego routera
  (ADR 0007), nie z liczby węzłów. Najlepsze wyniki kampanii są bliskie B z pilotażu (qaoa_n16 26 < 27; qft_n20 668
  > 654, bo przebiegi trwały 120 s zamiast 300 s).

Zagrożenia trafności: dwie sesje (inny stan zasilania laptopa, inna karta sieciowa laptopa – opóźnienie MPI 4× niższe
w sesji 2, przy komunikacji < 1 % czasu); n = 10 na komórkę; progi wyznaczone z 3 przebiegów pilotażu tylko na
konfiguracji 3-hostowej; populacje dobrane per węzeł, więc „dołożenie węzła” zmienia też łączną populację.

## Iteracja I – jakość względem SABRE (2026-11-24)

Konfiguracja: wszystkie 3 węzły (`rtx5080+rtx5070+rtx3060`), jeden przebieg 120 s na instancję, ziarno 1, bez celu
(`--target -1`), migracja `adaptiveB`, topologia `all`, populacje jak w kampanii. Wszystkie 12 wyników przechodzi
`validate-runs`. SABRE: minimum i mediana po 10 ziarnach z `results/baseline/sabre.csv`.
Dane: `results/quality-agg/quality.csv`, `results/quality-agg/aggregate.csv` (przepustowość).

| Instancja | GA (120 s) | SABRE min / mediana | GA / SABRE min | Przepustowość [mln ocen/s] | Laptop [mln/s] |
|---|---|---|---|---|---|
| qaoa_n8 | 8 | 6 / 6 | 1,33 | 13,4 | 4,91 |
| qaoa_n12 | 19 | 13 / 14 | 1,46 | 13,0 | 4,77 |
| qaoa_n16 | 27 | 19 / 20 | 1,42 | 12,6 | 4,67 |
| qaoa_n20 | 42 | 28 / 32 | 1,50 | 12,3 | 4,62 |
| qaoa_n24 | 58 | 35 / 41 | 1,66 | 12,4 | 4,61 |
| qaoa_n26 | 44 | 36 / 42 | 1,22 | 12,3 | 4,48 |
| qft_n8 | 48 | 17 / 18 | 2,82 | 12,8 | 4,74 |
| qft_n12 | 150 | 51 / 53 | 2,94 | 12,0 | 4,41 |
| qft_n16 | 342 | 100 / 106,5 | 3,42 | 11,2 | 4,07 |
| qft_n20 | 672 | 170 / 177 | 3,95 | 10,5 | 3,88 |
| qft_n24 | 1134 | 250 / 258,5 | 4,54 | 9,0 | 3,28 |
| qft_n27 | 1594 | 341 / 349 | 4,67 | 7,9 | 2,91 |

Obserwacje:
- **GA V0 nie dorównuje SABRE na żadnej instancji.** Dla QAOA luka wynosi 1,2–1,7×, dla QFT 2,8–4,7× i rośnie
  z rozmiarem obwodu. To skutek naiwnego routera zachłannego (ADR 0007): GA optymalizuje tylko layout początkowy,
  a router przesuwa zawsze kubit `a` po najkrótszej ścieżce bez patrzenia na kolejne bramki. QFT ma pary CX między
  wszystkimi kubitami, więc żaden layout nie ratuje zachłannego routingu; QAOA na grafie 3-regularnym ma mniej
  i bardziej lokalnych par. Wyniki zgadzają się z kampanią i pilotażem (qft_n12 150 jak w kampanii, qaoa_n16 27 wobec
  26 w kampanii przy 40 przebiegach), więc pojedynczy przebieg wystarcza do porównania rzędu wielkości.
- qaoa_n26 ma mniej SWAP-ów niż qaoa_n24 (44 wobec 58) i najmniejszą lukę – instancje QAOA to różne losowe grafy,
  więc liczba SWAP nie musi rosnąć monotonicznie z n.
- **Przepustowość spada z długością obwodu** (13,4 → 7,9 mln ocen/s od qaoa_n8 do qft_n27), bo koszt oceny rośnie
  z liczbą bramek 2q. Dotyczy to też laptopa (4,9 → 2,9 mln/s), więc próg z listy kontrolnej (ok. 4,5 mln/s) odnosi
  się do instancji kampanii (qaoa_n16, qaoa_n24); dla qft_n24/n27 niższa wartość nie oznacza trybu oszczędzania.
- Wniosek do iteracji II: poprawa jakości wymaga zmiany routera (lookahead), a nie większej mocy obliczeniowej –
  luka nie maleje mimo ok. 1–1,6 mld ocen na instancję.

Zagrożenia trafności: jeden przebieg na instancję (bez rozrzutu); SABRE to 10 krótkich przebiegów CPU (10 ziaren),
a GA – 120 s na 3 GPU – porównanie dotyczy jakości wyniku, nie kosztu.

## Iteracja I – profil V0 (Nsight, 2026-11-25)

Ustawienia: `qft_n12`, ziarno 1, wersja V0.

- `nsys profile -t cuda,nvtx`: jedna wyspa na każdej karcie (`--pop 262144`, 50 generacji) oraz przebieg na 3 hostach
  (20 s, `adaptiveB`, `all`, populacje 262 144 / 131 072 / 65 536).
- `ncu --set full -k regex:fitness -c 3`: 3 wywołania kernela fitness, 262 144 osobników (1024 bloki × 256 wątków).
- Raporty i zestawienia (`stats_*.csv`) leżą w `results/prof` (poza gitem). Komendy:
  [kampania-iteracja-1.md](kampania-iteracja-1.md), krok 7.

### Podział czasu generacji

Średnie z logów CSV (`eval_time_s`, `ops_time_s`, bez generacji 0). Zakresy NVTX (`ga_evaluate` względem `ga_run`)
dają te same proporcje.

| Przebieg | Karta (populacja) | ms / generację | ocena na GPU [ms] | operatory hosta [ms] | udział hosta | M ocen/s |
|---|---|---:|---:|---:|---:|---:|
| jedna wyspa | RTX 5080 (262 144) | 47,5 | 3,9 | 43,7 | 91,8 % | 5,43 |
| jedna wyspa | RTX 3060 (262 144) | 92,7 | 11,4 | 81,3 | 87,7 % | 2,77 |
| jedna wyspa | RTX 5070 Laptop (262 144) | 55,8 | 7,3 | 48,6 | 87,0 % | 4,52 |
| 3 hosty, rank 0 | RTX 5080 (262 144) | 43,7 | 4,5 | 39,2 | 89,6 % | 5,95 |
| 3 hosty, rank 1 | RTX 3060 (131 072) | 53,6 | 15,3 | 38,3 | 71,4 % | 2,43 |
| 3 hosty, rank 2 | RTX 5070 Laptop (65 536) | 14,2 | 2,7 | 11,5 | 80,9 % | 4,59 |

Ocena na GPU (zakresy NVTX, jedna wyspa, średnio na generację):

| Karta | `fitness_h2d` [ms] | `fitness_kernel` [ms] | `fitness_d2h` [ms] | kernel w `ga_evaluate` |
|---|---:|---:|---:|---:|
| RTX 5080 | 0,51 | 3,47 | 0,11 | 85 % |
| RTX 3060 | 0,90 | 10,32 | 0,22 | 90 % |
| RTX 5070 Laptop | 1,03 | 7,54 | 0,22 | 86 % |

`fitness_kernel` mierzy kernel razem z `cudaDeviceSynchronize`. Na RTX 5070 Laptop średnią podbija jedno wywołanie
trwające 85 ms; mediana wynosi 5,5 ms.

MPI widziane z hosta (przebieg na 3 hostach, 20 s):

- `mpi_migrate`: łącznie 4–7 ms na rank (średnio 3–18 µs na generację).
- `mpi_terminate`: 4–35 ms jednorazowo.

Oba koszty to poniżej 0,2 % czasu przebiegu. Jednorazowy koszt inicjalizacji CUDA (pierwszy `cudaMalloc`) wynosi
55–110 ms.

### Kernel fitness (Nsight Compute, pierwsze z 3 wywołań)

| Metryka | RTX 5080 | RTX 3060 | RTX 5070 Laptop |
|---|---:|---:|---:|
| czas pod `ncu` [ms] | 3,50 | 13,0 | 8,53 |
| przepustowość SM (`SM Throughput`) | 14,6 % | 20,3 % | 19,1 % |
| przepustowość pamięci (`Memory Throughput`, maksimum z poziomów niżej) | 91,8 % | 73,6 % | 94,8 % |
| przepustowość L1 (`L1/TEX Cache Throughput`) | 62,2 % | **87,8 %** | 81,0 % |
| przepustowość L2 (`L2 Cache Throughput`) | **91,8 %** | 73,6 % | **94,8 %** |
| przepustowość DRAM | 1,3 % | 27,1 % | 0,2 % |
| zajętość osiągnięta / teoretyczna | 85,4 / 100 % | 93,6 / 100 % | 90,0 / 100 % |
| rejestry na wątek | 38 | 22 | 38 |
| aktywne wątki na warp (z 32) | 19,3 | 19,3 | 19,3 |
| aktywność wydawania instrukcji (`issue active`) | 7,9 % | 8,4 % | 10,0 % |
| IPC | 0,31 | 0,34 | 0,40 |
| trafienia L1 / L2 | 90 / 99 % | 80 / 100 % | 92 / 100 % |
| dominujące przestoje warpów | `long_scoreboard`, `lg_throttle` | `lg_throttle`, `long_scoreboard` | `long_scoreboard`, `lg_throttle` |

Liczba instrukcji pamięci jest taka sama na wszystkich kartach:

- 16,5 mln odczytów i 19,5 mln zapisów pamięci lokalnej (tablice `l2p` / `p2l` w wątku);
- 13,0 mln odczytów globalnych (`pairs`, `dist`, `next_hop`);
- 0 odczytów pamięci współdzielonej.

Ruch w sektorach 32-bajtowych (RTX 5080; na pozostałych kartach prawie tak samo):

- zapisy pamięci lokalnej to 149 mln sektorów, a odczyty 93 mln. L1 przepuszcza zapisy do L2, więc do L2 trafia
  144 mln sektorów zapisu (ok. 4,6 GB na wywołanie) wobec 17 mln sektorów odczytu. Zapisy to 89 % ruchu L2;
- odczyty globalne to 13,0 mln żądań i 110 mln sektorów, czyli 8,5 sektora na żądanie warpu. Dostęp jest
  nieskoalesowany: każdy wątek czyta własny wiersz populacji (krok 27 B) i losowe elementy tablic `dist` /
  `next_hop`. `ncu` raportuje, że wątek wykorzystuje średnio 1,7 z 32 bajtów sektora.

`ncu` blokuje zegary, więc czasy kernela są w nim dłuższe niż w `nsys` (RTX 5080: 3,50 vs 3,13 ms).

### Obserwacje

1. **Wąskie gardło V0 to operatory GA na CPU.** Zajmują 87–92 % czasu generacji przy jednej wyspie i 71–90 % w
   przebiegu na 3 hostach. Przez ten czas GPU stoi bezczynnie, co widać na osi czasu `nsys` jako luki między
   kernelami. Z prawa Amdahla wynika, że nawet nieskończenie szybka ocena na GPU skróciłaby generację na RTX 5080
   najwyżej 1,09×. Usunięcie kosztu operatorów hosta dawałoby do 12×. To potwierdza wstępną obserwację z testu
   dymnego ([profilowanie.md](profilowanie.md)).
2. **Kopie H2D / D2H to 10–15 % oceny**, czyli 1–2 % generacji. Pamięć przypięta (pinned) i strumienie mają więc
   sens dopiero po przyspieszeniu hosta.
3. **Koszt MPI (migracja, zakończenie) jest pomijalny** (poniżej 0,2 %). Słabsza skalowalność konfiguracji
   wielowęzłowych nie wynika z komunikacji, tylko z niejednorodnego tempa wysp (zob. kampania TTT).
4. **Kernel jest ograniczony przepustowością pamięci podręcznych, nie obliczeniami ani DRAM.** Na RTX 5080 i RTX
   5070 Laptop (sm_120) wąskim gardłem jest L2 (92–95 %), na RTX 3060 (sm_86) L1 (88 %). Na kartach RTX 50xx DRAM
   jest prawie nieużywany (0,2–1,3 %), bo cały zbiór roboczy mieści się w L2. RTX 3060 ma znacznie mniejszą L2 i część
   ruchu (głównie zapisy pamięci lokalnej) trafia do DRAM (27 %). Wskazuje na to:
   - niska przepustowość SM (15–20 %) i aktywność wydawania instrukcji (8–10 %);
   - przestoje `long_scoreboard` (czekanie na dane z L1 / L2) i `lg_throttle` (pełna kolejka instrukcji lokalnych
     i globalnych), razem 95 % próbek.

   Głównym źródłem ruchu są zapisy dynamicznie indeksowanych tablic `l2p` / `p2l`. Trafiają one do pamięci
   lokalnej, a L1 przepuszcza je do L2 (89 % ruchu L2). Drugim źródłem są nieskoalesowane odczyty globalne
   (populacja, `dist` / `next_hop`; pamięć współdzielona nie jest używana). Rozbieżność wątków (19,3 z 32
   aktywnych) wynika z różnej liczby SWAP-ów u różnych osobników.

   Widok Source w `ncu` (RTX 5080) daje podobny obraz: 37,8 % próbek przestojów przypada na warunek pętli `while`
   w `route` (odczyt `dist` i `l2p`), 29,0 % na zapis `l2p[other]`, 12,4 % na odczyt `p2l[w]` i 6,2 % na odczyt
   `next_hop`. Odczyty `dist` / `next_hop` mają 57 % / 40 % nadmiarowych sektorów.

   Wstępna wersja tego punktu (2026-11-25) mówiła o potoku L1 / LSU na wszystkich kartach. Poprawiono ją po
   przejrzeniu sekcji Speed Of Light w `ncu-ui`, gdzie `Memory Throughput` jest równe `L2 Cache Throughput`.
5. Na RTX 3060 w przebiegu na 3 hostach kernel dla połowy populacji (131 072) trwał średnio 14,2 ms, a w
   pojedynczym profilu 10,2 ms dla 262 144, przy dużym rozrzucie (4,9–24,4 ms). Przyczyny nie wyjaśniono. Możliwe,
   że kartę obciążał wtedy pulpit albo czas zależy od zawartości populacji (liczby SWAP-ów). Do sprawdzenia przy
   kolejnym profilu.

Wniosek do iteracji II (bez zmian w V0): pierwszy kandydat wydajnościowy to operatory GA na hoście. Najpierw
drobniejsze zakresy NVTX (selekcja, krzyżowanie, mutacja), potem wielowątkowość albo przeniesienie operatorów na
GPU. Optymalizacje kernela mają sens dopiero potem. Najwięcej obiecuje usunięcie zapisów pamięci lokalnej, np.
trzymanie mapowania w rejestrach lub pamięci współdzielonej. Mniej: tabele w pamięci współdzielonej i
`__constant__`, kopie.

## Iteracja II – V1: równoległe operatory GA na CPU (2026-11-25)

**Diagnoza (profil V0):** operatory GA na CPU to 85–92 % czasu generacji, wykonywane jednowątkowo.

**Hipoteza:** potomkowie jednej generacji powstają niezależnie, więc pętla selekcja → krzyżowanie → mutacja da się
zrównoleglić; czas generacji powinien spaść kilkukrotnie (Amdahl: do ok. 12× przy zerowym koszcie operatorów).

**Zmiana** (`perf(ga)`, gałąź `perf/parallel-host-operators`):

- pętla tworzenia potomków i inicjalizacja populacji w `#pragma omp parallel for` (OpenMP, MSVC `/openmp`);
- każdy potomek ma własny generator SplitMix64 z ziarnem wyliczonym z (ziarno przebiegu, generacja, indeks), więc
  wynik nie zależy od liczby wątków (test `threads_independent`: 1 i 4 wątki dają identyczną populację w każdej
  generacji);
- flaga `--threads N` (domyślnie wszystkie wątki), liczba wątków w `summary.json`;
- elityzm i statystyki zostają szeregowe;
- strumień liczb losowych jest inny niż w V0, więc przebiegi V1 nie są identyczne z V0 dla tego samego ziarna.

**Szybki pomiar** (`qft_n12`, `--pop 262144`, 50 generacji, ziarno 1, jedna wyspa na karcie; czas `time_s`):

| Karta | V0 | V1, 1 wątek | V1, wszystkie wątki | przyspieszenie względem V0 |
|---|---:|---:|---:|---:|
| RTX 5080 (AMG-NORTH) | 2,46 s | 2,12 s | 0,35 s | 7,0× |
| RTX 3060 (PS04) | 4,83 s | – | 1,22 s | 4,0× |
| RTX 5070 Laptop (HyperX_Zuber) | 2,96 s | – | 0,71 s | 4,2× |

Czasy V0 pochodzą z profilu `nsys` (zakres `ga_run`, ten sam przebieg), więc zawierają drobny narzut profilera.
Na wszystkich kartach `best = 165`. V1 z jednym wątkiem jest szybsze od V0 (2,12 vs 2,46 s), bo generator
SplitMix64 jest tańszy od `std::mt19937_64`.

Na RTX 5080 generacja trwa teraz ok. 7 ms, z czego ok. 4 ms to ocena na GPU, więc GPU stało się główną częścią
generacji. Pełny pomiar (kampania TTT V1, profil `nsys` V1) jest w toku.


**Żaden z poniższych punktów nie jest zaimplementowany w v1.** To lista pomysłów do sprawdzenia po profilowaniu,
w kolejności wynikającej z wyników, a nie z tej listy. Profil V0 (sekcja wyżej) wskazuje najpierw operatory GA
na hoście; kernel i kopie są drugorzędne, a koszt MPI pomijalny.

- Topologia / interwał migracji przy niejednorodnych wyspach (szybka wyspa z małą populacją może narzucać swoje
  rozwiązanie pozostałym; zob. qaoa_n16 w kampanii TTT V0).
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
