# 0005. Deterministyczny baseline SABRE (jeden przebieg `SabreLayout`)

- Status: Zaakceptowana
- Data: 2026-11-03

## Kontekst

Prowadzący wymaga wyłączenia wszystkich ukrytych optymalizacji Qiskit. Baseline ma być powtarzalny i porównywalny z
GA: to samo wejście kanoniczne, liczba SWAP mierzona identycznie (`count_ops()['swap']`), ten sam walidator.
Szczegóły przebiegów presetu w [determinizm-qiskit.md](../determinizm-qiskit.md). Wersja Qiskit: 2.6.1.

## Rozważane opcje

- `optimization_level=3` bez zmian.
- `optimization_level=0` z metodami SABRE (`layout_method="sabre"`, `routing_method="sabre"`).
- Własny `PassManager` z pojedynczym przebiegiem `SabreLayout`.

## Decyzja

Własny `PassManager([SabreLayout(coupling_map, seed=seed, max_iterations=4, layout_trials=20, swap_trials=20)])`.
Wartości odpowiadają `SabreLayout` w poziomie 3 w Qiskit 2.6.1 (`builtin_plugins.py`, `DefaultLayoutPassManager`);
ustawiamy je jawnie. Środowisko: `QISKIT_SABRE_ALL_THREADS` usunięta, `RAYON_NUM_THREADS=1`, `QISKIT_PARALLEL=FALSE`.
Poziom 3 zostaje tylko jako opcjonalny wiersz `context_only` (liczba CX i głębokość).

## Argumenty za i przeciw

- Poziom 3 bez zmian: za: to, czego użyłby typowy użytkownik. Przeciw: VF2Layout/VF2PostLayout, ElidePermutations,
  konsolidacja i resynteza, anulowanie bramek oraz translacja bazy rozkładająca `swap` na 3 CX; liczba prób zależna
  od zmiennych środowiska; SWAP nie da się policzyć. Narusza wymaganie prowadzącego.
- Poziom 0 z metodami SABRE: za: mniej przebiegów. Przeciw: nadal dochodzą `UnitarySynthesis`/`HighLevelSynthesis`,
  `BasisTranslator` (usuwa `swap`), domyślna liczba prób (5), osobny `SabreSwap` z heurystyką `basic`, więc parametry
  nie odpowiadają poziomowi 3 i trudniej udowodnić, co dokładnie się wykonało.
- Własny `PassManager`: za: dokładnie jeden przebieg, jawne parametry, `swap` zostaje w wyniku, test może to
  sprawdzić (`to_flow_controller().tasks`). Przeciw: nie jest to „gotowy preset”; zależy od zachowania `SabreLayout`
  (osadzenie na kubitach fizycznych robi sam przebieg) w danej wersji Qiskit, więc zapisujemy wersję w CSV.

## Konsekwencje

- Wynik baseline to liczba SWAP routera SABRE (z layoutem), gotowa do porównania z GA.
- `layout.json` i QASM mają tę samą konwencję co wynik C++ (zob. [0006](0006-walidacja.md)).
- Zmiana wersji Qiskit może zmienić wyniki; `uv.lock` i kolumna `qiskit_version` to dokumentują.
- Wiersz `level3_*` służy wyłącznie kontekstowi i nie może być celem TTT.
