# Sprawozdanie – iteracja I (V0): skąd brać materiały

Katalog `results/` **nie jest w repozytorium** – pliki wynikowe przekazujemy osobno jako archiwum (komenda na końcu).
Liczby i wnioski są opisane w [wyniki.md](wyniki.md) (sekcje „Iteracja I – pomiary węzłów”, „koszt komunikacji MPI”,
„pilotaż i progi TTT”, „kampania TTT V0”), decyzje w [dziennik-decyzji.md](dziennik-decyzji.md), a większe decyzje w
[ADR](adr/README.md).

## 1. Wprowadzenie

- **1.2 Środowisko** – tabela węzłów (CPU, RAM, GPU, VRAM, sterownik, CUDA 12.9, karta sieciowa):
  `results/bench/bench_<host>_node.json`. Kolejność węzłów jak w szablonie: 5080, 3060, 5070 Laptop. Adresy i sieć:
  [instalacja-windows.md](instalacja-windows.md). Wersja kodu: hash commitu (`git rev-parse HEAD`).
- **1.3 Architektura** – schemat Python → C++ GA → CUDA fitness, połączenie przez MPI: opis w
  [architektura.md](architektura.md); schemat rysujemy ręcznie.
- **1.4 FakeKolkataV2** – `results/figures/topology_falcon27.pdf` (27 kubitów z numerami),
  tabela `results/figures/device_falcon27.tex`.

## 2. Metodyka

- **2.1 Instancje** – `results/figures/instances.tex` (lub `.csv`); przykładowe obwody `circuit_qft5.pdf`,
  `circuit_qaoa6.pdf`.
- **2.2 Formaty** – QASM 2.0 + `layout.json`; przykładowy rekord: `summary.json` z przykładowego przebiegu w archiwum.
- **2.3 Metryki** – TTT do progu T_ε = ⌈(1+ε)·B⌉ dla ε = 0,10 / 0,05 / 0,02; mediana z cenzurowaniem (przebieg
  bez sukcesu = +∞, kwartyle liczone tak samo); przyspieszenie i efektywność heterogeniczna. Definicje:
  [protokol-eksperymentow.md](protokol-eksperymentow.md), ADR 0010 i ADR 0012.
- **2.4 Walidacja** – CheckMap, test strukturalny, globalny wektor stanu (fidelity ≥ 1−1e-8):
  [walidacja.md](walidacja.md); rysunek routingu przed i po z widocznymi SWAP-ami:
  `results/figures/routing_before_after.pdf`.
- **2.5 Profilowanie** – wersje narzędzi i zrzuty Nsight: lista 11 zrzutów i co ma być na każdym w rozdziale 8
  [kampania-iteracja-1.md](kampania-iteracja-1.md). **Jeszcze nie wykonane.**
- **2.6 Protokół** – 4 konfiguracje × 4 instancje × 10 powtórzeń, 120 s na przebieg, populacje
  262 144 / 65 536 / 131 072, migracja `adaptiveB`, topologia `all`: [kampania-iteracja-1.md](kampania-iteracja-1.md).

## 3. Wersja V0

- **3.2 Poprawność** – wszystkie 160 przebiegów przeszło walidację (kolumny `validated`, `valid` w
  `results/aggregate.csv`).
- **3.3 Wyniki**
  - Przepustowość w zależności od populacji: `results/plots/bench_sweep.png` + tabele w [wyniki.md](wyniki.md).
    Podział czasu: **89–92 % czasu generacji to operatory GA na CPU** – główne wąskie gardło V0.
  - Profil Nsight V0 (tabele w [wyniki.md](wyniki.md), sekcja „profil V0”): zestawienia
    `results/prof/stats_*.csv`. Operatory hosta to 87–92 % generacji, kopie 1–2 %, MPI poniżej 0,2 %. Kernel jest
    ograniczony przepustowością L2 (RTX 50xx) lub L1 (RTX 3060): zapisy pamięci lokalnej `l2p`/`p2l`, nieskoalesowane
    odczyty globalne. Wykresy
    `results/plots/prof_*.png`, opis, podpisy i gotowy akapit z wnioskiem:
    [profil-v0-dla-sprawozdania.md](profil-v0-dla-sprawozdania.md).
  - Zegar, moc, temperatura GPU: `results/plots/bench_nvsmi_<host>.png`.
  - TTT: tabela z `results/aggregate.csv` (gotowa w [wyniki.md](wyniki.md)) i ECDF
    `results/plots/ttt_ecdf_<instancja>_<eps>.png`. Do tekstu 2–4 wykresy (np. qft_n20 przy ε 0,05, qaoa_n16 przy
    ε 0,10), reszta do załącznika.
  - Zbieżność: `results/plots/convergence_runs_rtx5080+rtx5070+rtx3060_qft_n20_rep1.png`.
  - Jakość względem SABRE (wszystkie 12 instancji): `results/quality-agg/quality.csv`, tabela w [wyniki.md](wyniki.md)
    – GA 1,2–1,7× więcej SWAP-ów dla QAOA, 2,8–4,7× dla QFT, luka rośnie z rozmiarem QFT (naiwny router, ADR 0007).
    Proponowany wykres: słupki GA / SABRE min na instancję (dane z tego pliku). `results/quality.csv` to ta sama
    miara tylko dla 4 instancji kampanii.

## 5. Koszt rozproszenia

- Przyspieszenie i efektywność: kolumny `speedup`, `efficiency` w `results/aggregate.csv` (tabela w
  [wyniki.md](wyniki.md)).
- Ping-pong MPI: `results/bench/mpi_pingpong.csv`, `mpi_pingpong_2026-11-23.csv`, wykres
  `results/plots/mpi_pingpong.png` (z `bench-plot`).

## 6. ADR

Tabela z [adr/README.md](adr/README.md) i [dziennik-decyzji.md](dziennik-decyzji.md).

## Zagrożenia trafności (2.6 lub 3.3)

- Kampania w dwóch sesjach (powtórzenia 1–4 i 5–10).
- Laptop liczył w sesji 2 o ok. 50 % szybciej (3,06 → 4,61 mln ocen/s), najpewniej przez ustawienia zasilania;
  logów `nvidia-smi` z kampanii brak.
- Między sesjami zmieniono kartę sieciową laptopa (opóźnienie MPI ok. 4× niższe w sesji 2).
- n = 10 powtórzeń na komórkę; progi z 3 przebiegów pilotażu.

## Jeszcze brakuje

- Trzy zrzuty z GUI Nsight Compute (Speed Of Light, Memory Chart, widok Source; Roofline nie ma zastosowania):
  [profil-v0-dla-sprawozdania.md](profil-v0-dla-sprawozdania.md), ostatnia sekcja. Pozostałe widoki zastępują
  wykresy `prof-plot`.

## Przygotowanie archiwum

W oknie uv, w katalogu repozytorium:

```powershell
uv run python -m qmap.experiments bench-plot --bench results\bench --out results\plots
uv run python -m qmap.experiments prof-plot
Compress-Archive -Force -Path results\figures, results\plots, results\bench, results\aggregate.csv, results\quality.csv, results\quality-agg, results\pilot\targets.csv, results\prof\stats_*.csv, results\runs\rtx5080\qft_n12\rep1 -DestinationPath sprawozdanie_iter1.zip
```

Przykładowy przebieg (`rtx5080/qft_n12/rep1`) daje wzór `summary.json` i logu `run_r0.csv`. Wykresy profilu są w
`results\plots\prof_*.png`, a dwa zrzuty z GUI Nsight Compute trafiają do `results\figures`.
