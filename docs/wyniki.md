# Dziennik iteracji i wyników

Każda iteracja optymalizacji ma ten sam układ: **wersja → profil → wąskie gardło → poprawka → zysk**.
Poprawki wprowadzamy dopiero po zmierzeniu (profilowanie Nsight / pomiar czasu), nie na zapas.

## v1 (naiwna wersja bazowa)

- Wersja: _do uzupełnienia_ (commit, parametry GA, sprzęt).
- Profil: _do uzupełnienia_.
- Wąskie gardło: _do uzupełnienia_.
- Poprawka: brak (v1 jest punktem odniesienia).
- Zysk: brak.

## Kandydaci do optymalizacji

**Żaden z poniższych punktów nie jest zaimplementowany w v1.** To lista pomysłów do sprawdzenia po profilowaniu,
w kolejności wynikającej z wyników, a nie z tej listy.

- Lookahead w routerze (uwzględnienie kilku kolejnych bramek 2q przy wyborze SWAP) – mniej SWAP, droższy fitness.
- Pomijanie kolejnych identycznych par CX (powtórzona para, już sąsiednia, nie wymaga ponownego sprawdzania).
- Populacja i ocena fitness na GPU (jeden wątek / osobnik).
- Pamięć przypięta (pinned) i strumienie CUDA – nakładanie transferów i obliczeń.
- Tabele `dist` / `next_hop` w pamięci współdzielonej.
