# 0007. Chromosom = layout początkowy, fitness = liczba SWAP zachłannego routera

- Status: Zaakceptowana
- Data: 2026-11-04

## Kontekst

GA musi ocenić bardzo wiele osobników, więc funkcja przystosowania ma być tania, deterministyczna i możliwa do
policzenia na GPU (kolejny etap) oraz identycznie na CPU (emisja obwodu, testy). Wynik musi dać się zwalidować
tym samym walidatorem co baseline SABRE ([0006](0006-walidacja.md)) i porównać z nim liczbą SWAP
([0005](0005-baseline-sabre.md)).

## Rozważane opcje

- Chromosom kodujący także kolejność/wybór SWAP-ów (GA szuka całego routingu).
- Chromosom = permutacja kubitów fizycznych (layout początkowy); routing deterministyczny.
- Osobna implementacja routera dla GPU i dla CPU.
- Jedna procedura routingu w nagłówku, używana na hoście i na urządzeniu.

## Decyzja

- Chromosom to permutacja kubitów fizycznych (27 genów); logiczny kubit `i` startuje na kubicie `chromosom[i]`
  (konwencja `initial` z `layout.json`). Dla `n < 27` pierwsze `n` genów to layout, reszta to ancille.
- Fitness to liczba SWAP wstawionych przez deterministyczny zachłanny router: dla każdej bramki 2q `(a, b)`
  w kolejności obwodu, dopóki odległość `dist[l2p[a]][l2p[b]] > 1`, przesuwamy kubit `a` o jeden krok wzdłuż
  najkrótszej ścieżki. Krok to `next_hop` – sąsiad o **najniższym indeksie** leżący na najkrótszej ścieżce.
  Tabele `dist` i `next_hop` (`uint8_t`, `n_phys × n_phys`) liczymy raz z grafu urządzenia (BFS) i sprawdzamy
  z macierzą `distance` z JSON.
- Jedna procedura `qmap::route` (`cpp/include/qmap/router.hpp`) w nagłówku z makrem `QMAP_HD`
  (`__host__ __device__` pod `nvcc`), bez kontenerów STL, wyjątków i alokacji. Emisja obwodu na hoście
  (`route_circuit`) wywołuje tę samą procedurę z funktorem zapisującym wstawione SWAP-y, więc obwód
  walidowany w Pythonie powstaje z tego samego kodu, który liczy fitness.
- Wersja v1 jest celowo naiwna: bez lookahead, bez pomijania powtórzonych par CX, bez sprytnego rozstrzygania
  remisów. Usprawnienia wprowadzamy później jako osobne, sprofilowane iteracje (zob. [wyniki.md](../wyniki.md)).

## Argumenty za i przeciw

### Chromosom = layout, router deterministyczny
- Za: mała przestrzeń poszukiwań (permutacje), operatory permutacyjne są standardowe; fitness jest funkcją
  samego chromosomu, więc powtarzalny i łatwy do zrównoleglenia (jeden wątek / osobnik).
- Przeciw: jakość ograniczona przez router; GA nie poprawi decyzji, które router podejmuje źle.

### Chromosom kodujący routing
- Za: potencjalnie lepsze wyniki.
- Przeciw: ogromna, silnie ograniczona przestrzeń (poprawność ścieżek), trudne operatory, trudne porównanie z SABRE.

### Dwie implementacje (CPU i GPU)
- Za: każda może być zoptymalizowana osobno.
- Przeciw: ryzyko rozjazdu wyników; fitness z GPU mógłby nie odpowiadać obwodowi, który walidujemy.

### Jedna procedura `__host__ __device__`
- Za: jedno źródło prawdy, testy na CPU dotyczą kodu uruchamianego na GPU.
- Przeciw: ograniczenia kodu urządzenia (tablice o stałym rozmiarze, brak STL), kod musi pozostać prosty.

## Konsekwencje

- Pozytywne: deterministyczny fitness, ten sam kod w testach, emisji i (później) w jądrze CUDA; test krzyżowy
  w Pythonie weryfikuje wynik C++ walidatorem z ADR 0006.
- Negatywne / ryzyka: v1 daje więcej SWAP niż SABRE; zachłanny router nie widzi przyszłych bramek, a powtórzone
  pary CX są przeliczane ponownie (to świadome pole do optymalizacji, nie błąd). Tabele `uint8_t` ograniczają
  urządzenie do 254 kubitów fizycznych (wartość `0xFF` oznacza pustą pozycję / brak ścieżki).
