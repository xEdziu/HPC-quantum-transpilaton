# Walidacja routingu

Ten sam walidator (`python/qmap/validate.py`) sprawdza wynik GA i wynik SABRE. Wejście: obwód oryginalny
(instancja), obwód po routingu (QASM), layout początkowy i końcowy.

## Konwencja layoutu

- `initial`, `final`: listy długości n (liczba kubitów logicznych); wpis `i` to indeks kubitu fizycznego,
  w którym leży kubit logiczny `i` na początku / na końcu obwodu.
- Obwód po routingu ma 27 kubitów (`n_phys`), bramki działają na indeksach fizycznych i należą do
  `{rz, sx, x, cx}` oraz `swap`.
- Plik pomocniczy `layout.json`: `{"initial": [...], "final": [...], "swaps": int}` (`swaps` opcjonalne).
- QASM wczytujemy przez `qmap.instances.loads` (starsze definicje bramek Qiskit, bo `sx` nie ma w ścisłym `qelib1.inc`).

## Trzy testy

1. **CheckMap.** `PassManager([CheckMap(coupling_map)])` na obwodzie po routingu; każda bramka 2q (w tym `swap`)
   musi leżeć na krawędzi mapy sprzężeń (symetrycznej, z `load_device()`).
2. **Test strukturalny (dokładny, O(bramek)).** Przechodzimy obwód śledząc mapowanie fizyczne → logiczne
   (kubit bez wpisu to ancilla). `swap` wymienia wpisy, każda inna bramka jest przemapowana na kubity logiczne;
   bramka na ancilli to błąd. Wymagamy, aby wyprowadzony layout końcowy równał się podanemu `final` oraz aby
   `circuit_to_dag(odtworzony) == circuit_to_dag(oryginał)`. Równość DAG jest semantyczna, więc toleruje
   zmianę kolejności niezależnych bramek.
3. **Globalny wektor stanu.** Losowy prep V (losowe `U(θ,φ,λ)` na każdym kubicie, CX na parach `(i,i+1)`
   dla parzystego `i`, kolejna warstwa `U`) jest stosowany na kubitach layoutu początkowego, potem obwód R,
   potem `(V·C)†` ustawione na layoucie końcowym. Prawdopodobieństwo stanu |0…0⟩ na **wszystkich**
   27 kubitach to fidelity.

## Dlaczego prep/unprep jest poprawnym testem globalnym

Fidelity wynosi `|⟨0|(C V)†_final R V_init|0⟩|²`. Jest równe 1 wtedy i tylko wtedy, gdy R działa jak C
z dokładnością do permutacji layoutu i fazy globalnej na podprzestrzeni, w której ancille są w |0⟩.
Dla ciągłego, losowego V zachodzi to z prawdopodobieństwem 1 (stały V mógłby nie wykryć błędu, np. gdyby
stan wejściowy był własnym stanem błędnej bramki). Splątująca warstwa CX sprawia, że błędy
na kubitach połączonych bramkami też zmieniają stan. Rzut na |0…0⟩ wszystkich kubitów sprawdza
przy okazji, że ancille wracają do |0⟩.

Parametry: `k = 3` niezależnych V (ziarna `seed, seed+1, seed+2`), tolerancja `tol = 1e-8`
(fidelity ≥ 1 − tol dla każdego V). Symulacja: Aer `statevector`, `precision="double"`,
`save_amplitudes_squared([0])`.

## Użycie z linii komend

```powershell
uv run python -m qmap.validate --instance qft_n8 --routed out\qft_n8.qasm --layout out\layout.json --k 3
```

Raport JSON (`check_map`, `structural`, `fidelities`, `statevector`, `swaps`, `valid`) trafia na stdout;
kod wyjścia 0 gdy `valid`, 1 w przeciwnym razie.

## Koszt

Wektor stanu 27 kubitów w `complex128` to 2^27 · 16 B = 2 GiB. Aer odrzuca kubity, których żadna bramka nie dotyka,
więc dla mniejszych n pamięć jest niższa. Koszt czasu to k symulacji; test strukturalny i CheckMap są pomijalne.
`Operator.equiv` stosujemy wyłącznie w testach dla n ≤ 8 (macierz 2^n × 2^n).
