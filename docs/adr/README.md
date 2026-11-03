# Rejestr decyzji architektonicznych (ADR)

Format: [MADR](https://adr.github.io/madr/), po polsku. Szablon: [0000-szablon.md](0000-szablon.md).

## Indeks

| Nr | Tytu³ | Status |
|----|-------|--------|
| [0001](0001-windows-ms-mpi.md) | Natywny Windows 11 + MS-MPI | Zaakceptowana |
| [0002](0002-podzial-python-cpp-cuda.md) | Podzia³ Python / C++ / CUDA | Zaakceptowana |
| [0003](0003-urzadzenie-falcon27.md) | Urz¹dzenie Falcon 27q, routing nieskierowany, bez szumu | Zaakceptowana |
| [0004](0004-obwody-kanoniczne.md) | Obwody kanoniczne | Zaakceptowana |
| [0006](0006-walidacja.md) | Walidacja: test strukturalny + globalny wektor stanu | Zaakceptowana |

## Jak dodaæ ADR

1. Skopiuj `0000-szablon.md` do `NNNN-krotki-opis.md` (kolejny wolny numer, nazwa ma³ymi literami, myœlniki).
2. Wype³nij wszystkie sekcje; ustaw status (Zaproponowana / Zaakceptowana / Odrzucona / Zast¹piona przez NNNN).
3. Dopisz wiersz do indeksu powy¿ej.
4. Zmianê decyzji zapisujemy nowym ADR, a w starym zmieniamy status na "Zast¹piona przez NNNN".
