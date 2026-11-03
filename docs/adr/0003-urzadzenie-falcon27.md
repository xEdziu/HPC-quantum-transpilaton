# 0003. Urządzenie Falcon 27q, routing nieskierowany, bez szumu

- Status: Zaakceptowana
- Data: 2026-11-03

## Kontekst

Potrzebujemy realistycznej, ale małej topologii docelowej oraz jednoznacznej definicji, czym jest "poprawny routing".

## Rozważane opcje

- Urządzenie: IBM Falcon 27q (`FakeKolkataV2`), albo syntetyczna siatka/linia.
- Routing: na grafie nieskierowanym, albo z uwzględnieniem kierunku CX (`GateDirection`).
- Szum: z modelem szumu, albo bez.

## Decyzja

- Topologia i baza natywna `{rz, sx, x, cx}` z **IBM Falcon 27q** (`FakeKolkataV2`).
- Routing na grafie **nieskierowanym**; kierunek CX jest poza zakresem (bez przejścia `GateDirection`).
- **Bez modelu szumu.**

## Argumenty za i przeciw

### Falcon 27q
- Za: realna, powszechnie znana topologia heavy-hex; wystarczająco duża, by problem był nietrywialny.
- Za: pełny wektor stanu 27 kubitów (2^27 liczb zespolonych `complex128` = 2 GiB) mieści się w pamięci,
  co umożliwia walidację "globalnym wektorem stanu" całego rozwiązania.
- Przeciw: tylko jedno urządzenie, więc wnioski nie uogólniają się na inne topologie.

### Routing nieskierowany
- Za: liczba SWAP nie zależy od kierunku CX; prostszy router i fitness.
- Przeciw: wynik nie jest bezpośrednio wykonywalny na sprzęcie bez odwracania CX (poza zakresem projektu).

### Brak szumu
- Za: szum nie wpływa na liczbę SWAP, a uniemożliwiłby walidację przez równoważność stanów/operatorów.
- Przeciw: nie oceniamy jakości wykonania na realnym sprzęcie.

## Konsekwencje

- Opis urządzenia (krawędzie, macierz odległości, baza) eksportujemy do commitowanego JSON.
- Symetrię mapy sprzężeń sprawdzamy jawnie; gdyby nie zachodziła, symetryzujemy i odnotowujemy w pliku.
- Walidator może porównywać pełne wektory stanu na wszystkich 27 kubitach.
