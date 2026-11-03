# Rejestr decyzji architektonicznych (ADR)

Format: [MADR](https://adr.github.io/madr/), po polsku. Szablon: [0000-szablon.md](0000-szablon.md).

## Indeks

| Nr | Tytuł | Status |
|----|-------|--------|
| [0001](0001-windows-ms-mpi.md) | Natywny Windows 11 + MS-MPI | Zaakceptowana |
| [0002](0002-podzial-python-cpp-cuda.md) | Podział Python / C++ / CUDA | Zaakceptowana |
| [0003](0003-urzadzenie-falcon27.md) | Urządzenie Falcon 27q, routing nieskierowany, bez szumu | Zaakceptowana |
| [0004](0004-obwody-kanoniczne.md) | Obwody kanoniczne | Zaakceptowana |
| [0005](0005-baseline-sabre.md) | Deterministyczny baseline SABRE (jeden przebieg `SabreLayout`) | Zaakceptowana |
| [0006](0006-walidacja.md) | Walidacja: test strukturalny + globalny wektor stanu | Zaakceptowana |

## Jak dodać ADR

1. Skopiuj `0000-szablon.md` do `NNNN-krotki-opis.md` (kolejny wolny numer, nazwa małymi literami, myślniki).
2. Wypełnij wszystkie sekcje; ustaw status (Zaproponowana / Zaakceptowana / Odrzucona / Zastąpiona przez NNNN).
3. Dopisz wiersz do indeksu powyżej.
4. Zmianę decyzji zapisujemy nowym ADR, a w starym zmieniamy status na "Zastąpiona przez NNNN".
