# 0006. Walidacja: test strukturalny + globalny wektor stanu + CheckMap

- Status: Zaakceptowana
- Data: 2026-11-03

## Kontekst

Prowadzący wymaga walidacji globalnego wektora stanu całego rozwiązania. Wynik GA i wynik SABRE musi
sprawdzać ten sam walidator, a obwód po routingu ma 27 kubitów z ancillami.

## Rozważane opcje

- Równość `Operator` dla całego obwodu.
- Sam `CheckMap` (poprawność względem mapy sprzężeń).
- `mqt.qcec` (weryfikacja równoważności obwodów).
- Test strukturalny + globalny wektor stanu + `CheckMap`.

## Decyzja

Wybieramy trzy testy łącznie (opis w [walidacja.md](../walidacja.md)): `CheckMap`, test strukturalny
(odwrócenie routingu i równość DAG) oraz globalny wektor stanu z losowym splątującym prepem V
(k = 3, tolerancja 1e-8) w Aer.

## Argumenty za i przeciw

- `Operator`: za: dokładny. Przeciw: macierz 2^n × 2^n, nieosiągalna powyżej ok. 14 kubitów; zostaje w testach dla n ≤ 8.
- Sam `CheckMap`: za: tani. Przeciw: nie dowodzi równoważności (nie wykryje usuniętego SWAP ani zmienionej bramki).
- `mqt.qcec`: za: gotowe narzędzie. Przeciw: dodatkowa zależność, mniejsza kontrola nad layoutem i ancillami, a wymaganie dotyczy wprost wektora stanu.
- Wybrane rozwiązanie: za: test strukturalny jest dokładny i tani, wektor stanu dostarcza globalnego dowodu wymaganego przez prowadzącego, `CheckMap` łapie naruszenia topologii; sprawdza też ancille.
- Wybrane rozwiązanie: przeciw: wektor stanu 27 kubitów zajmuje 2 GiB i jest probabilistyczny (równoważność z prawdopodobieństwem 1 dla losowego V, z k = 3 powtórzeniami).

## Konsekwencje

- Wspólna konwencja layoutu (`initial`, `final`, `layout.json`) dla routera C++ i baseline SABRE.
- Walidacja pełnej instancji 27q wymaga ok. 2 GiB RAM.
- Testy jednostkowe używają n = 8 i kontrolują przypadki negatywne (usunięty SWAP, zły layout końcowy, zmieniony kąt `rz`).
