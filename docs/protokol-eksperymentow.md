# Protokół eksperymentów

Pytania badawcze:

1. **TTT dla konfiguracji klastra**: `{rtx5080}`, `{rtx5080+rtx3060}`, `{rtx5080+rtx5070}`, `{rtx5080+rtx5070+rtx3060}`.
   Czy dodanie słabszego węzła skraca czas do celu? (przyspieszenie i efektywność heterogeniczna wg Alby).
2. **Jakość**: liczba SWAP najlepszego rozwiązania GA względem SABRE ([ADR 0005](adr/0005-baseline-sabre.md)).

Dokładna, uporządkowana lista poleceń kampanii iteracji I (z mapowaniem na sekcje sprawozdania):
[kampania-iteracja-1.md](kampania-iteracja-1.md). Poniżej opis metodyki.

Definicja TTT, poziomy celu i obsługa przebiegów cenzurowanych: [ADR 0010](adr/0010-ttt.md). **Wszystkie trzy poziomy
ε mierzymy w jednym przebiegu** (`--target <T_0.10>,<T_0.05>,<T_0.02>`, koniec przy najostrzejszym): [ADR 0012](adr/0012-wiele-progow-ttt.md).
Narzędzie:
`python/qmap/experiments.py` (`uv run python -m qmap.experiments <plan|pilot|aggregate|validate-runs|plot|bench-plot>`).
Wartości liczbowe poniżej (instancje, limity czasu, liczba powtórzeń) to **propozycje startowe**; po pilotażu
zespół zatwierdza je i zapisuje w [wyniki.md](wyniki.md).

## Parametry stałe

Dla wszystkich konfiguracji i powtórzeń: ta sama wersja kodu (jeden commit), `--tournament`, `--pc`, `--pm`, `--elite`
domyślne, `--migration adaptiveB`, `--topology all`, ten sam `--max-time`. Zmienia się wyłącznie zestaw hostów
(każdy host ze swoim `--pop` z `data/cluster.json`), instancja, poziom celu i ziarno. Ziarno = numer powtórzenia
(1…10), **to samo dla każdej konfiguracji** (porównanie sparowane; ziarno rangi pochodzi z `rank_seed`, więc rangi różnią
się między sobą).

## Kroki

1. **Ten sam commit na wszystkich hostach.** `git pull`, `git rev-parse HEAD` identyczny na każdym komputerze;
   zbuduj Release (README) i uruchom `.\scripts\check-env.ps1` (same `[ OK ]`). Układ katalogów taki sam
   ([instalacja-windows.md](instalacja-windows.md), Krok 7), `smpd` jako `hpc` na wszystkich 3 hostach, zapora,
   test łączności z okna konta `hpc`:
   `mpiexec -timeout 30 -genv MPICH_NETMASK 192.168.66.0/255.255.255.0 -hosts 3 192.168.66.61 1 192.168.66.2 1 192.168.66.222 1 hostname`.
2. **Konfiguracja klastra.** `data\cluster.example.json` zawiera nasz klaster (IP, maska sieci dla `-genv MPICH_NETMASK`);
   `copy data\cluster.example.json data\cluster.json` i ewentualnie popraw `pop`
   (nazwa konfiguracji = nazwy hostów z `cluster.json` połączone `+`; pierwszy host to rank 0 i komputer, z którego
   uruchamiamy skrypt). `pop` dobieramy tak, aby jedna generacja trwała znacznie krócej niż limit TDR systemu Windows
   (domyślnie ok. 2 s) i karta była nasycona; zapisz dobrane wartości.
3. **Stan sprzętu przed serią.** Na każdym hoście `nvidia-smi -q -d CLOCK,TEMPERATURE,PERFORMANCE -f results\nvsmi_<host>.txt`
   oraz zamknięcie programów obciążających GPU/CPU. Laptop (RTX 5070 Laptop): zasilacz, tryb wydajności, chłodzenie;
   pozostaje w normalnym użyciu (zagrożenie niżej). Pomiary pojedynczych węzłów i MPI: `scripts\bench-node.ps1`,
   `qmap_mpi_bench` (kroki 2–3 w runbooku); ciągły log `nvidia-smi` przez całą kampanię na każdym hoście.
4. **Pilotaż (wyznaczenie B).** Długi przebieg na całym klastrze, bez celu (`--target -1`), po kilka ziaren na instancję:

   ```powershell
   uv run python -m qmap.experiments plan --cluster data\cluster.json --instances qft_n12 qft_n16 qft_n20 qaoa_n12 qaoa_n16 qaoa_n20 --configs rtx5080+rtx5070+rtx3060 --target -1 --reps 3 --max-time 300 --out results/pilot --script pilot.ps1
   .\pilot.ps1
   uv run python -m qmap.experiments pilot --runs results/pilot --out results/pilot/targets.csv
   ```

   `targets.csv` (`instance,B,eps,target`) zawiera T_ε = ⌈(1 + ε)·B⌉ dla ε ∈ {0,10, 0,05, 0,02}. B to najlepszy
   wynik z pilotażu; jeśli któryś pilotażowy przebieg pokonał SABRE albo wynik znacząco się poprawia przy dłuższym
   czasie, wydłuż pilotaż i powtórz.
5. **Plan.** Cztery konfiguracje × instancje × 10 powtórzeń (**jeden przebieg na powtórzenie** ze wszystkimi trzema
   progami ε; katalog `<konfiguracja>\<instancja>\rep<k>`); `--max-time` dobierz z pilotażu (kampania iteracji I: 60 s,
   instancje `qft_n12`, `qft_n20`, `qaoa_n16`, `qaoa_n24`):

   ```powershell
   uv run python -m qmap.experiments plan --cluster data\cluster.json --targets-from results/pilot/targets.csv --instances qft_n12 qft_n20 qaoa_n16 qaoa_n24 --reps 10 --max-time 60 --out results/runs --script run_all.ps1
   ```

6. **Przebieg.** `.\run_all.ps1` z komputera rank 0, w katalogu repo. Skrypt jest wznawialny (pomija przebiegi, których
   `summary.json` już istnieje); jedna komenda `mpiexec` na przebieg. Przebiegi są **szeregowe** (jeden na raz), żeby
   nie dzielić kart. Skrypt przeplata konfiguracje w czasie (najpierw powtórzenie 1 dla wszystkich
   konfiguracji, potem powtórzenie 2 itd.), aby dryf termiczny i obciążenie tła nie obciążały jednej konfiguracji.
7. **Zebranie danych.** Rank 0 zapisuje `summary.json`, `best.qasm`, `best.json`, `run_r0.csv`; pozostałe rangi
   zapisują `run_r<k>.csv` na **własnym** hoście w tej samej ścieżce względnej. Skopiuj je do tych samych katalogów
   na komputerze rank 0: `.\scripts\collect-results.ps1 -Cluster data\cluster.json -Path results\runs` (robocopy przez
   udział administracyjny `\\<IP>\C$`, z okna konta `hpc`). Bez nich przepustowość i efektywność w agregacie będą `NaN`.
8. **Agregacja.**

   ```powershell
   uv run python -m qmap.experiments aggregate --runs results/runs --sabre results/baseline/sabre.csv --out results
   ```

   (`aggregate` i `plot` biorą z `--targets`, domyślnie `results/pilot/targets.csv`, odwzorowanie progów na ε.)

   Tworzy `results\aggregate.csv` (na konfigurację, instancję i ε: n, liczba i częstość sukcesów, mediana TTT, kwartyle
   sukcesów, przepustowość, przyspieszenie, efektywność) i `results\quality.csv` (najlepszy SWAP GA vs minimum i mediana
   SABRE; baseline: `uv run python -m qmap.baseline`).
9. **Walidacja** najlepszych rozwiązań (k = 1, jak w baseline):

   ```powershell
   uv run python -m qmap.experiments validate-runs --runs results/runs
   uv run python -m qmap.experiments aggregate --runs results/runs
   ```

   `validate-runs` zapisuje `validate.json` obok każdego `best.json`; kolumny `validated` i `valid` w agregacie
   liczą sprawdzone i poprawne przebiegi. Wynik GA, który nie przechodzi walidacji, jest błędem, nie wynikiem.
10. **Wykresy.** `uv run python -m qmap.experiments plot --runs results/runs --out results/plots`
    (ECDF czasu do celu na instancję i ε, krzywa kończy się poniżej 1, gdy są przebiegi cenzurowane); krzywe
    zbieżności wybranego przebiegu: `--convergence results/runs/<config>/<instancja>/rep<k>`.

## Co raportujemy

- Na (konfiguracja, instancja, ε): częstość sukcesu w limicie, mediana TTT (**dolna mediana z cenzurowaniem**: przebiegi
  bez sukcesu liczone jako +∞; mediana jest określona tylko gdy sukces ≥ 50 %, inaczej `NaN`), kwartyle TTT liczone tak samo
  (k-ta najmniejsza wartość, k = ⌈q·n⌉, z porażkami jako +∞; `NaN`, gdy wypada na porażkę), więc zawsze
  Q1 ≤ mediana ≤ Q3 i zgadzają się z wykresem ECDF.
- Przyspieszenie `S = mediana TTT(rtx5080) / mediana TTT(konfiguracja)`; przepustowość = suma po rangach średniej
  `evals_per_s`; efektywność heterogeniczna `E = S / (przepustowość(konfiguracja) / przepustowość(rtx5080))`.
  E < 1: węzeł dodaje mniej, niż wynikałoby z jego mocy obliczeniowej.
- Jakość: najlepszy SWAP GA na instancję vs minimum i mediana SABRE (10 ziaren).
- Wersję kodu (commit), parametry GA, `pop` na host, sprzęt, liczbę powtórzeń i limit czasu.

## Zagrożenia dla ważności

- **Zegary hostów nie są zsynchronizowane.** TTT liczymy od wyjścia z `MPI_Barrier` na każdej randze
  ([ADR 0010](adr/0010-ttt.md)); błąd rzędu opóźnienia sieci (ms) jest pomijalny dla przebiegów trwających sekundy,
  ale nie porównujemy czasów bezwzględnych między hostami.
- **Termika i zasilanie.** Laptop (RTX 5070 Laptop) może dławić zegary pod długim obciążeniem; wynik zależy od
  kolejności przebiegów. Dlatego: logi `nvidia-smi` przed serią, przeplatanie konfiguracji w czasie, powtórzenia, mediana zamiast średniej.
- **Laptop w normalnym użyciu.** Właściciel używa laptopa (RTX 5070 Laptop, 192.168.66.222) podczas pomiarów i nie
  możemy tego zabronić. Obciążenie tła (CPU, GPU, sieć) może wydłużać czasy konfiguracji z laptopem i zwiększać rozrzut.
  Nie eliminujemy tego, tylko dokumentujemy: ciągły log `nvidia-smi` (użycie GPU, zegar SM, temperatura, moc, powody
  dławienia) na laptopie przez całą kampanię (`results\bench\nvsmi_campaign_<host>.csv`), migawki „top CPU” z
  `bench-node.ps1`, przeplatanie konfiguracji i powtórzenia; w sprawozdaniu jako zagrożenie trafności.
- **Progi zależne.** Trzy progi ε jednej instancji mierzone w jednym przebiegu dzielą trajektorię; analizujemy je
  osobno na ε i nie traktujemy jako niezależnych prób ([ADR 0012](adr/0012-wiele-progow-ttt.md)).
- **Obciążenie tła** (aktualizacje, antywirus, inne programy na GPU/CPU) i **współdzielenie GPU** (Windows może używać
  karty do wyświetlania obrazu): zamknąć programy, ten sam stan komputerów w całej serii.
- **Limit TDR.** Zbyt duża generacja (duże `pop` na słabej karcie) powoduje reset sterownika; dobór `pop` w kroku 2.
- **Sieć LAN.** Współdzielenie łącza z innym ruchem zmienia opóźnienie migracji; sieć ustawiona jako prywatna, bez
  dużych transferów podczas serii.
- **Cenzurowanie.** Za krótki limit czasu przy małym ε daje większość przebiegów cenzurowanych i `NaN` w medianie; wtedy
  raportujemy tylko częstość sukcesu albo wydłużamy limit.
- **Jedna instancja urządzenia i niewielka liczba ziaren** (10): wnioski dotyczą tych instancji, nie ogółu obwodów.
- **`pop` różni się między konfiguracjami** (każdy host ma własne `pop`); porównujemy konfiguracje sprzętowe, nie
  algorytmy przy stałej populacji globalnej.
