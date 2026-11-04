# Determinizm i wyłączone optymalizacje Qiskit (baseline SABRE)

Prowadzący wymaga wyłączenia wszystkich ukrytych optymalizacji Qiskit. Baseline (`python/qmap/baseline.py`) ma być
deterministyczny i porównywalny z GA: to samo wejście kanoniczne, liczba SWAP mierzona tak samo (`count_ops()['swap']`).
Wersja Qiskit przy pisaniu tego dokumentu: **2.6.1**. Zapisujemy ją w każdym wierszu `sabre.csv`.

## Co robi `optimization_level=3` (źródło: `qiskit/transpiler/preset_passmanagers/`)

Lista zrobiona na podstawie `builtin_plugins.py` i `common.py` zainstalowanej wersji.

| Etap | Przebiegi (poziom 3) | Dlaczego zniekształca porównanie |
|------|----------------------|----------------------------------|
| init | `UnitarySynthesis`, `HighLevelSynthesis`, `BasisTranslator` dla bramek 3q+; `ElidePermutations`; `RemoveDiagonalGatesBeforeMeasure`; `RemoveIdentityEquivalent`; `InverseCancellation`; `CommutativeCancellation`; `ConsolidateBlocks`; `Split2QUnitaries` | Zmieniają obwód przed routingiem, więc router nie dostaje naszego wejścia kanonicznego. `ElidePermutations` i `Split2QUnitaries` (z `split_swap=True`) usuwają lub przenoszą permutacje, czyli zmieniają samą liczbę SWAP. |
| layout | `VF2Layout` (limit wywołań 30 000 000), potem `SabreLayout` (`max_iterations=4`, `swap_trials=20`, `layout_trials=20`) | `VF2Layout` szuka layoutu bez żadnego SWAP (doskonałe dopasowanie podgrafu); jeśli znajdzie, SABRE w ogóle nie działa. To inny algorytm, nie SABRE. |
| routing | `SabreSwap` (`heuristic="decay"`, `trials=20`) z kontrolą VF2 (`vf2_call_limit`, `vf2_max_trials`) | Dodatkowa próba znalezienia layoutu przez VF2 po routingu. |
| translation | `BasisTranslator` do `{rz, sx, x, cx}` | Rozkłada `swap` na 3 CX, więc SWAP znikają z obwodu i nie da się ich policzyć. |
| optimization | pętla do punktu stałego: `TwoQubitPeepholeOptimization`, `RemoveIdentityEquivalent`, `Optimize1qGatesDecomposition`, `CommutativeCancellation`, `ContractIdleWiresInControlFlow`; po pętli `VF2PostLayout` | Konsolidują i ponownie syntetyzują bloki 2q, więc SWAP rozpływają się z sąsiednimi CX. `VF2PostLayout` zmienia layout po routingu. |
| liczba prób | `_get_trial_count`: przy ustawionym `QISKIT_SABRE_ALL_THREADS` (lub opcji `sabre_all_threads` w pliku konfiguracji użytkownika) liczba prób rośnie do `max(liczba_CPU, 20)` | Liczba prób zależałaby od maszyny, więc wynik nie byłby powtarzalny między naszymi 3 komputerami. |

Wniosek: wynik `optimization_level=3` to liczba CX po wielu transformacjach, a nie liczba SWAP routera. Nie nadaje się
jako cel TTT ani jako baseline jakości routingu.

## Jak baseline tego unika

- `PassManager([SabreLayout(coupling_map, seed=seed, max_iterations=4, layout_trials=20, swap_trials=20)])`:
  **jeden** przebieg. `SabreLayout` bez `routing_pass` sam wykonuje routing i osadzenie obwodu na 27 kubitach
  fizycznych. Brak VF2, konsolidacji, syntezy, anulowania bramek, `ElidePermutations` i translacji bazy, dlatego `swap`
  zostaje w obwodzie i da się go policzyć. Dowodzi tego test `test_pass_manager_has_only_sabre_layout`.
- Parametry są jawne (`SABRE_PARAMS`) i odpowiadają użyciu `SabreLayout` w poziomie 3 (wartości wzięte z
  `builtin_plugins.py`, `DefaultLayoutPassManager`). Nie zależą od liczby rdzeni.
- Ziarno `seed` jest przekazywane do `SabreLayout`; to samo ziarno daje ten sam obwód (test porównuje `qasm2.dumps`).
- Zmienne środowiskowe ustawiane na początku modułu, **przed** `import qiskit`:
  - `QISKIT_SABRE_ALL_THREADS` jest usuwana;
  - `RAYON_NUM_THREADS=1` (`setdefault`), bo pula wątków Rust jest tworzona przy pierwszym użyciu, a jej rozmiar czytany
    jest wtedy z tej zmiennej;
  - `QISKIT_PARALLEL=FALSE` wyłącza własną pulę procesów Qiskit (`utils/parallel.py`).
- Uwaga: opcja `sabre_all_threads` w pliku konfiguracji użytkownika Qiskit (`~/.qiskit/settings.conf`) działa tylko w
  presetach poziomów, których baseline nie używa, więc nie ma wpływu na `SabreLayout` tworzony wprost.
- Kontrola dozwolonych bramek: po routingu zbiór nazw bramek musi należeć do `{rz, sx, x, cx, swap}`.

## Użycie

```powershell
uv run python -m qmap.baseline --validate
uv run python -m qmap.baseline --instances qft_n8 qaoa_n8 --seeds 0 1 2 --out results\baseline --context-level3
```

Domyślnie: wszystkie 12 instancji, ziarna 0..9, katalog wyjściowy `results/baseline` (jest w `.gitignore`).
Dla każdej pary (instancja, ziarno) powstają `<instancja>_s<ziarno>.qasm`, `<instancja>_s<ziarno>.layout.json`
(`{"initial", "final", "swaps"}`, format z [walidacja.md](walidacja.md)) oraz wiersz w `sabre.csv`
(`instance,n,seed,swaps,depth,two_qubit_count,seconds,qiskit_version,valid,level3_cx,level3_depth`).
Na końcu wypisywane są minimum i mediana SWAP dla każdej instancji.

`--validate` uruchamia ten sam walidator co dla GA, ale z `k = 1` (jedno losowe V), żeby symulacja 27 kubitów
(2 GiB, jedna symulacja) była znośna; pełne `k = 3` daje `uv run python -m qmap.validate`.

## Wiersz `context_only` (poziom 3)

Z flagą `--context-level3` do wiersza trafiają `level3_cx` i `level3_depth` z
`generate_preset_pass_manager(optimization_level=3, backend=FakeKolkataV2, seed_transpiler=seed)`. To wyłącznie
kontekst dla czytelnika (jak daleko jest „pełny” transpiler pod względem liczby CX). Nie jest celem TTT, a SWAP-ów nie
da się z niego policzyć, bo są rozłożone i zoptymalizowane. Nie wolno porównywać go z liczbą SWAP z GA.
