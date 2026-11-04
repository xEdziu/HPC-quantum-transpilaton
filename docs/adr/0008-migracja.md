# 0008. Asynchroniczna migracja najlepszego osobnika

- Status: Zaakceptowana
- Data: 2026-11-04

## Kontekst

GA działa na kilku wyspach: jeden rank MPI = jedna wyspa = jedna karta GPU, każda wyspa ma własny rozmiar
populacji (heterogeniczny klaster RTX 5080 / 5070 Laptop / 3060, tryb MPMD). Wyspy mają się wymieniać dobrymi
rozwiązaniami **bez czekania na siebie**: wolniejsza wyspa nie może blokować szybszej (to jest sens asynchronicznego
modelu wyspowego i głównej metryki TTT, zob. [0010](0010-ttt.md)). Trzeba zdecydować: co migruje, dokąd, jak często
i co się dzieje z przybyszem.

## Rozważane opcje

- Co migruje: najlepszy osobnik (jeden) / kilka osobników / losowy osobnik.
- Topologia: każdy do każdego (`all`) / pierścień (`ring`, rank `r` wysyła do `(r+1) % N`).
- Interwał: stały co N generacji (`fixed:N`) / adaptacyjny schemat A / adaptacyjny schemat B / interwał w czasie.
- Przybysz: zastępuje najgorszego osobnika / zastępuje losowego / trafia do osobnej puli.

## Decyzja

- Migruje **jeden osobnik: najlepszy dotychczasowy w danej wyspie** (wliczając przybyszy), czyli 27 genów + `int`
  fitness w jednym komunikacie (`TAG_MIGRANT`). Fitness jest dołączony, żeby odbiorca nie musiał go liczyć
  (i żeby od razu wiedzieć, czy to poprawa).
- Wysyłanie jest nieblokujące (`MPI_Isend`), odbiór przez `MPI_Iprobe` + `MPI_Recv` raz na generację, w haku
  `run_ga` (`IslandHook`, [GA](0007-chromosom-router.md)); nic nie czeka na komunikat, który jeszcze nie nadszedł.
- Topologia wybierana flagą `--topology all` (domyślnie) lub `ring`.
- Interwał wybierany flagą `--migration fixed:N | adaptiveA | adaptiveB` (domyślnie `fixed:100`), liczony w
  **generacjach**.
- Przybysz **nadpisuje aktualnie najgorszego osobnika** (geny i fitness) w miejscu; jeśli jest lepszy od najlepszego
  dotychczasowego, aktualizuje też wynik wyspy. Dzięki temu przybysz od razu uczestniczy w selekcji i elitaryzmie
  w następnej generacji, a populacja nie rośnie.
- Schematy adaptacyjne wg A. Mambrini, D. Sudholt, *Design and Analysis of Schemes for Adapting Migration Intervals
  in Parallel Evolutionary Algorithms*, Evolutionary Computation 23(4), 2015
  ([preprint](https://eprints.whiterose.ac.uk/93025/1/ECJ-submission-R1.pdf)). Obie wersje zaczynają od τ = 1:
  - **Schemat A**: gdy wyspa znajdzie ściśle lepszego najlepszego (własną ewolucją albo od przybysza), natychmiast
    rozsyła go i ustawia τ = 1; po τ generacjach bez ściślej lepszego wyniku migruje bieżącego najlepszego i
    podwaja τ.
  - **Schemat B**: migruje wyłącznie na końcu okresu (τ generacji od poprzedniej migracji); następnie
    τ := ⌈τ/2⌉ (nie mniej niż 1), jeśli w okresie była ściśle lepsza wartość (własna lub od przybysza), w przeciwnym
    razie τ := 2τ.
  - τ jest ograniczone od góry przez `max_generations` (brak przepełnienia). Regułę aktualizacji realizuje czysta
    funkcja `next_interval(Scheme, tau, improved, cap)` testowana bez MPI.
  - Zmiana względem pseudokodu z artykułu: wyspa wysyła swojego najlepszego dotychczasowego, a nie „najlepsze
    dziecko ostatniej generacji”; przy elitaryzmie to ta sama wartość lub lepsza.

## Argumenty za i przeciw

### Najlepszy osobnik, nieblokująco, przybysz za najgorszego (wybrana)
- Za: najprostszy wariant zgodny z modelem z artykułu (migruje kopia najlepszego); mały komunikat (31 bajtów),
  brak synchronizacji; przybysz nie zmienia rozmiaru populacji.
- Przeciw: tylko jeden osobnik na migrację, więc mała różnorodność przybyszy; przy częstych migracjach populacje
  mogą się upodobnić (przedwczesna zbieżność).

### Kilka osobników / losowy osobnik
- Za: większa różnorodność.
- Przeciw: większy ruch w sieci i więcej parametrów; wykracza poza najprostszą wersję v1 (zob. kandydaci w
  [wyniki.md](../wyniki.md)).

### Topologia `all` a `ring`
- `all`: za – informacja o poprawie dociera do wszystkich w jednym kroku; przeciw – ruch rośnie jak N² (dla 3
  wysp bez znaczenia).
- `ring`: za – ruch liniowy względem N, wolniejsze rozchodzenie się informacji (przydatne do porównania);
  przeciw – opóźnienie propagacji o N−1 kroków. Oba warianty zostają, bo różnią się tym, co mierzymy (TTT).

### Interwał stały a adaptacyjny
- Stały: za – prosty, przewidywalny; przeciw – trzeba dobrać N do problemu.
- Adaptacyjne A/B: za – same dobierają częstość migracji (często przy postępie, rzadko przy stagnacji) i mają
  udowodnione granice dla elitarnych EA; przeciw – więcej logiki; B zmienia τ dopiero na końcu okresu, więc
  reaguje wolniej, ale wysyła mniej komunikatów.

### Interwał w generacjach, nie w sekundach
- Wyspy mają różne rozmiary populacji i różne GPU, więc „generacja” trwa u każdej inaczej; interwał w sekundach
  oznaczałby u każdej inną liczbę generacji między migracjami i zależałby od zegara. Interwał w generacjach jest
  deterministyczny, zgodny z artykułem (analiza w generacjach) i nie wymaga pomiaru czasu w haku. Wada: szybka wyspa
  migruje częściej w sekundach niż wolna. Interwał oparty na czasie jest zapisany jako kandydat do optymalizacji.

## Konsekwencje

- Pozytywne: brak blokowania między wyspami; logika migracji jest w jednej klasie `Island` (`qmap_mpi`), a
  `run_ga` zna tylko haka; reguła τ jest testowalna jednostkowo.
- Negatywne / ryzyka: v1 jest naiwna: osobny bufor sterty na każdy `MPI_Isend`, `MPI_Test` w pętli po wszystkich
  oczekujących wysyłkach, jedna migracja = jeden osobnik; przybysz nadpisuje najgorszego nawet gdy sam jest od niego
  gorszy (rzadkie). W schemacie A przybysz będący poprawą jest rozsyłany dalej, co przy `all` daje powtórzenia
  (ograniczone, bo odbiorcy poprawiają się ściśle co najwyżej raz na wartość).
