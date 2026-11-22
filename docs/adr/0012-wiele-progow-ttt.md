# 0012. Wiele progów TTT mierzonych w jednym przebiegu

- Status: Zaakceptowana
- Data: 2026-11-22

## Kontekst

[ADR 0010](0010-ttt.md) definiuje TTT dla poziomów celu T_ε = ⌈(1 + ε)·B⌉, ε ∈ {0,10; 0,05; 0,02}. Naiwna realizacja
to osobny przebieg dla każdego ε (`--target T_ε`), czyli 3 × więcej przebiegów w kampanii: 4 konfiguracje × 4
instancje × 3 poziomy × 10 powtórzeń. Czas kampanii na trzech zajętych komputerach jest ograniczony.

## Rozważane opcje

- Osobny przebieg na każdy próg (stan dotychczasowy).
- Jeden przebieg na (konfiguracja, instancja, powtórzenie) z listą progów: wyspa zapisuje czas pierwszego osiągnięcia
  każdego progu, a przebieg kończy się na najostrzejszym.
- Odczyt czasów z logu CSV (`best` w funkcji `elapsed_s`) po przebiegu, bez zmian w kodzie.

## Decyzja

`qmap_ga --target 172,164,161` przyjmuje listę progów. Każdy rank zapisuje dla każdego progu czas (ten sam zegar co
`ttt_s`, od wyjścia z `MPI_Barrier`), w którym jego najlepszy dotychczasowy wynik (łącznie z imigrantami) po raz
pierwszy stał się ≤ progowi. Rank 0 redukuje każdy czas przez `MPI_MIN` po rangach (wartownik = „nigdy”), dokładnie
jak `ttt_s`. Przebieg kończy się (powód `target`, STOP do pozostałych rang jak dotąd), gdy najlepszy wynik wyspy jest
≤ **najmniejszemu** progowi. `ttt_s` w stdout i `summary.json` to czas najostrzejszego progu (zgodność wsteczna);
`summary.json` zawiera dodatkowo `"targets"` i `"ttt_by_target"` (klucze: progi jako napisy, `null` = nieosiągnięty),
a wiersz `global` na stdout pole `ttt_by_target=172:1.23,164:NA,...`.

Zmiana dotyczy wyłącznie pomiaru: algorytm GA, fitness, router i migracja są bez zmian, więc wydajność wersji V0
nie ulega zmianie (jedna pętla po 1–3 liczbach na generację).

## Dlaczego to nie zniekształca TTT

- Czas pierwszego osiągnięcia progu T jest zdarzeniem w trajektorii przebiegu o danym ziarnie. Trajektoria (jej
  część do chwili zatrzymania) nie zależy od tego, jakie progi mierzymy ani na którym się zatrzymujemy. Przebieg
  zatrzymany na T_0,10 zakończyłby się wcześniej, ale w chwili osiągnięcia T_0,10 wyglądałby identycznie jak przebieg,
  który idzie dalej do T_0,02. Czas pierwszego osiągnięcia T_0,10 jest więc ten sam.
- Subtelność: przebieg z jednym, luźniejszym progiem zatrzymałby się wcześniej (STOP do pozostałych rang) i inne
  rangi nie zdążyłyby już poprawić wyniku. Nie zmienia to czasu pierwszego osiągnięcia luźniejszego progu (STOP jest
  wysyłany dopiero po jego osiągnięciu), ale w przebiegu z wieloma progami rangi pracują dalej, więc to jedyna
  różnica: dłuższy ogon po osiągnięciu luźniejszego progu, który nie wchodzi do jego TTT.
- Przebiegi asynchroniczne nie są powtarzalne co do bitu (czas komunikatów zależy od sieci), więc „to samo ziarno”
  oznacza ten sam punkt startowy populacji, nie tę samą trajektorię; różnica nie wynika z wielu progów, tylko
  z natury MPI, i jest uwzględniona przez powtórzenia.
- Progi tej samej instancji w jednym przebiegu są zależne (jedna trajektoria). Dla różnych ε analizujemy je osobno
  (ECDF i mediany na ε); nie traktujemy ich jako niezależnych prób przy wnioskowaniu.

## Cenzurowanie

Próg nieosiągnięty w limicie `--max-time` jest cenzurowany tak jak w [ADR 0010](0010-ttt.md) (`null` w
`ttt_by_target`, liczony jako +∞ w mediany z cenzurowaniem). Ponieważ przebieg kończy się dopiero na najostrzejszym
progu albo limicie czasu, **ostrzejszy próg wydłuża każdy przebieg do limitu**, gdy nie jest osiągany, ale nie
zmienia czasów progów już osiągniętych. Limit czasu jest wspólny dla wszystkich progów danego przebiegu.

## Argumenty za i przeciw

### Jeden przebieg z listą progów (wybrana)
- Za: ok. 3 × krótsza kampania; progi mierzone na tej samej trajektorii (dobre do porównań między ε); stoper i
  redukcja już istnieją.
- Przeciw: progi zależne; przebiegi z wieloma progami trwają dłużej niż przebieg z jednym luźnym progiem.

### Osobny przebieg na próg
- Za: niezależne próby dla każdego ε.
- Przeciw: 3 × więcej przebiegów; niedostępne w naszym budżecie czasu.

### Czasy z logu CSV po przebiegu
- Za: brak zmian w kodzie.
- Przeciw: log jest zapisywany per rank i na hostach zdalnych, a czas jest rozdzielczością generacji; `best`
  w logu nie obejmuje momentu migracji w obrębie generacji tak samo jak `ttt_s`. Gorsza zgodność z ADR 0010.

## Konsekwencje

- Pozytywne: kampania pomiarowa krótsza ok. 3 ×; jeden `summary.json` na przebieg; test CPU pilnuje monotoniczności
  (łagodniejszy próg ≤ ostrzejszy).
- Negatywne / ryzyka: wszystkie rangi muszą dostać tę samą listę progów (inaczej redukcja MPI się rozjedzie);
  `plan` generuje identyczną listę dla każdej rangi. Struktura katalogów przebiegu zmienia się na
  `<konfiguracja>\<instancja>\rep<k>`.
