# 0009. Protokół zakończenia bez gubienia komunikatów

- Status: Zaakceptowana
- Data: 2026-11-04

## Kontekst

Wyspy migrują asynchronicznie ([0008](0008-migracja.md)), więc w chwili zakończenia pracy przez rank w sieci mogą
być jeszcze komunikaty (migranci, sygnały stopu), a rank może mieć niezakończone `MPI_Isend`. Przy zwykłym
`MPI_Finalize` takie komunikaty są niedozwolone (niedopasowane wysyłki/odbiory), a jedna wyspa, która osiągnęła
cel, powinna zatrzymać pozostałe (inaczej TTT nie ma sensu, [0010](0010-ttt.md)). Wymagania: brak zawieszeń,
brak zgubionych ani nieodebranych komunikatów przy `MPI_Finalize`.

## Rozważane opcje

- `MPI_Cancel` oczekujących wysyłek i odbiorów.
- Zliczanie komunikatów (`MPI_Alltoall` liczników), wyczerpanie kolejki i dopiero `MPI_Waitall`.
- Token / barierowe wygaszanie (np. `MPI_Barrier` w pętli GA).

## Decyzja

Brak `MPI_Cancel`. Protokół (klasa `Island`, metoda `finish`, zakres NVTX `mpi_terminate`):

1. Komunikaty: `TAG_MIGRANT` (31 bajtów: 27 genów + `int` fitness) i `TAG_STOP` (pusty ładunek).
2. Rank, którego GA zatrzymało się z powodu `target`, wysyła `TAG_STOP` (`MPI_Isend`, śledzony jak migranci) do
   **wszystkich** pozostałych rang, niezależnie od topologii. Rangi kończące z powodu `max_time` / `max_generations` /
   `remote_stop` nie wysyłają stopu. Rank, który odbierze `TAG_STOP` w haku (`MPI_Iprobe`), zwraca `false`; GA kończy
   z powodem `remote_stop`.
3. Po pętli GA rank **przestaje wysyłać** i wykonuje:
   - a) wysłanie stopu (tylko przy `target`),
   - b) `MPI_Alltoall` liczników wysłanych komunikatów (migranci + stopy) do każdego adresata,
   - c) dla każdego nadawcy `src` odbiera dokładnie `wysłane_do_mnie[src] − odebrane_od[src]` pozostałych
     komunikatów (`MPI_Recv`, dowolny tag; migranci są zliczani, stopy odrzucane),
   - d) `MPI_Waitall` na wszystkich własnych niezakończonych `MPI_Isend`, zwolnienie buforów,
   - e) redukcje wyników: `MPI_Allreduce(MINLOC)` na (fitness, rank), rozesłanie genów najlepszego z właściciela
     (`MPI_Bcast`), `MPI_Reduce(MIN)` czasu TTT (rangi bez celu: wartość wartownicza), `MPI_Reduce(SUM)` liczby
     ewaluacji, `MPI_Reduce(MAX)` czasu całkowitego,
   - f) `MPI_Finalize` (destruktor `MpiSession`).

### Dlaczego nie ma zakleszczenia i nic nie ginie

- Krok b to operacja kolektywna, do której **każdy** rank dochodzi: żadna pętla GA nie czeka na komunikat, a pętle
  kończą się po `max_generations` / `max_time` / `target` / otrzymaniu stopu. Rank, który skończył wcześniej, czeka
  w `MPI_Alltoall`, a jego komunikaty odbierają dalej ranki jeszcze pracujące (hak działa i tak do końca ich pętli).
- Po powrocie z kroku b liczniki są **ostateczne**: żaden rank nie wyśle już niczego (pętla GA się skończyła, stop
  został już wysłany w kroku a, przed b). Zatem `wysłane_do_mnie[src] − odebrane_od[src]` to dokładnie liczba
  komunikatów, które były już zgłoszone przez `MPI_Isend`, więc każdy z odbiorów w kroku c ma gwarantowanego
  nadawcę; żaden nie czeka na komunikat, który nie powstanie.
- Wszystkie oczekujące komunikaty są odebrane (c) **przed** `MPI_Waitall` (d). Komunikaty są krótkie (≤ 31 B,
  protokół eager), ale nawet gdyby wysyłka wymagała spotkania z odbiorem, odbiory z kroku c są już wystawione u
  wszystkich rang, a postęp MPI zachodzi wewnątrz `MPI_Recv` / `MPI_Waitall`, więc wysyłki się kończą.
- Po kroku d żaden komunikat nie jest w locie, więc kolektywy z kroku e i `MPI_Finalize` są bezpieczne.
- Jednoczesne stopy od kilku rang są po prostu odrzucane w kroku c.

## Argumenty za i przeciw

### Zliczanie + Alltoall (wybrana)
- Za: deterministyczna poprawność (wiemy dokładnie, ile komunikatów odebrać); tylko standardowe funkcje MPI 2.2
  dostępne w MS-MPI ([0001](0001-windows-ms-mpi.md)).
- Przeciw: rank, który skończył z powodu `max_time` / `max_generations`, czeka w `MPI_Alltoall` na pozostałych, a
  pozostali dowiadują się o jego końcu dopiero przez własne limity (świadomie: tylko `target` wysyła stop).

### `MPI_Cancel`
- Za: brak zliczania.
- Przeciw: anulowanie wysyłek jest w MPI zawodne (wysyłka może się mimo to zrealizować, nieokreślony stan);
  trudne do uzasadnienia poprawności. Odrzucone.

### Bariery w pętli GA
- Przeciw: synchronizacja zabija asynchroniczność, której chcemy.

## Konsekwencje

- Pozytywne: brak zawieszeń i brak nieodebranych komunikatów (testy `qmap_mpi_tests` sprawdzają, że suma wysłanych =
  suma odebranych migrantów); jeden rank z celem zatrzymuje całą grupę.
- Negatywne / ryzyka: liczniki to `int`; skrajnie długie przebiegi z bardzo małym interwałem mogłyby go
  przekroczyć (nierealne w tym projekcie). Protokół zakłada, że każdy rank dochodzi do `finish`; wyjątek na jednym
  ranku kończy się `MPI_Abort` całej grupy. Zachowanie na prawdziwej sieci LAN (opóźnienia, zapora) do
  zweryfikowania przez zespół.
