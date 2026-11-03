# 0004. Obwody kanoniczne

- Status: Zaakceptowana
- Data: 2026-11-03

## Kontekst

SABRE (baseline) i nasz GA muszą dostawać dokładnie ten sam obwód wejściowy, a liczba SWAP musi być
jednoznaczna: żadne bramki SWAP nie mogą pochodzić z samego obwodu logicznego.

## Rozważane opcje

- Obwody w dowolnej bazie, transpilowane z optymalizacją.
- Obwody w bazie natywnej `{rz, sx, x, cx}` po samej translacji bazy.

## Decyzja

- Baza kanoniczna `{rz, sx, x, cx}` uzyskana przez `transpile(..., optimization_level=0, seed_transpiler=0)`,
  czyli wyłącznie translację; SABRE i GA konsumują identyczne wejście.
- **QFT** bez końcowych logicznych SWAP (`synth_qft_full(n, do_swaps=False)`), by nie mylić ich z SWAP z routingu.
- **QAOA** p=1 MaxCut na losowym grafie 3-regularnym (`networkx.random_regular_graph`, stałe ziarno 2026);
  stałe kąty (gamma=0.7, beta=0.3). Kąty nie wpływają na routing, są stałe dla powtarzalności.
- Rozmiary: QFT n ∈ {8, 12, 16, 20, 24, 27}; QAOA n ∈ {8, 12, 16, 20, 24, 26}.
  Graf 3-regularny wymaga parzystego n, więc n=27 jest niemożliwe i używamy 26.
- Pliki instancji (`data/instances/*.qasm`) i opis urządzenia (`data/devices/falcon27.json`) są commitowane.

## Argumenty za i przeciw

- Za: jednakowe wejście dla obu metod; brak pomiarów, barier i resetów; routing można liczyć po `swap`.
- Za: commitowane pliki czynią eksperymenty niezależnymi od wersji `qiskit-ibm-runtime` i generatorów losowych `networkx`.
- Przeciw: baza natywna zwiększa liczbę bramek względem bazy wysokopoziomowej (np. RZZ rozkłada się na CX+RZ).
- Przeciw: pliki QASM w repozytorium zajmują miejsce (kilka tysięcy linii na instancję), co jest akceptowalne.

## Konsekwencje

- Parser QASM w C++ wystarczy dla podzbioru `rz`, `sx`, `x`, `cx` (bez definicji `gate`).
- Instancje regenerujemy poleceniem `uv run python -m qmap.instances`; zmiana wersji bibliotek nie zmienia commitowanych danych.
