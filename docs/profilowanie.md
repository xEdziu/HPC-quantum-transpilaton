# Profilowanie

Profilujemy **przed** każdą optymalizacją ([wyniki.md](wyniki.md): wersja → profil → wąskie gardło → poprawka → zysk).
Poniższe komendy dotyczą tego projektu (Windows 11, MS-MPI, CUDA 12.9, karty sm_86 i sm_120). Profilowania uruchamia
użytkownik; zapisane raporty (`.nsys-rep`, `.ncu-rep`) są ignorowane przez git.

> Komendy z [kampania-iteracja-1.md](kampania-iteracja-1.md), krok 7, sprawdzono 2026-11-25 na wszystkich trzech
> hostach (Nsight Systems 2025.1.3, Nsight Compute 2025.2.1). Znane problemy i obejścia opisuje sekcja
> „Znane problemy na naszych komputerach” niżej.
>
> Profilowanie iteracji I (wersja V0): dokładne komendy dla każdej karty i przebiegu 3-hostowego oraz lista zrzutów
> ekranu z GUI: [kampania-iteracja-1.md](kampania-iteracja-1.md), kroki 7–8. Ścieżka `nsys` na naszych komputerach:
> `C:\Program Files\NVIDIA Corporation\Nsight Systems 2025.1.3\target-windows-x64\nsys.exe`. Tło obciążenia i
> zegary GPU dokumentuje log `nvidia-smi` (kampania, krok 0).

## Narzędzia

| Narzędzie | Do czego | Uwagi |
|---|---|---|
| Nsight Systems (`nsys`) | oś czasu: kernele, kopie H2D/D2H, wywołania CUDA API, zakresy NVTX | pierwsze narzędzie; odpowiada na pytanie „gdzie jest czas” |
| Nsight Compute (`ncu`) | wnętrze jednego kernela: roofline, pamięć, zajętość, widok źródła | dopiero gdy `nsys` wskaże kernel jako wąskie gardło |
| NVTX (`nvtx3`, `CUDA::nvtx3`) | nazwane zakresy w kodzie, widoczne w `nsys` | już w kodzie, zob. niżej |
| CUPTI | warstwa pod `nsys` i `ncu` (zbieranie zdarzeń i liczników GPU) | nie używamy bezpośrednio |
| `py-spy`, `cProfile` | strona Pythona: `qmap.validate`, `qmap.baseline`, `qmap.experiments` | `uv run python -m cProfile -s cumtime -m qmap.validate ...` |

**Nie używamy** NVVP ani `nvprof`: nie wspierają kart o compute capability ≥ 7.5 (nasze karty: sm_86 i sm_120),
a NVVP usunięto z CUDA 13. Decyzja: [ADR 0011](adr/0011-narzedzia-profilowania.md).

Kernele są kompilowane z `--generate-line-info` (zob. `cpp/CMakeLists.txt`), więc `ncu` pokazuje widok źródła
(powiązanie SASS z liniami `.cu`) bez budowania wersji debug.

## Zakresy NVTX w kodzie

| Zakres | Miejsce | Co mierzy |
|---|---|---|
| `ga_run` | `qmap_ga` | cała pętla GA (bez inicjalizacji CUDA/MPI) |
| `ga_evaluate` | `qmap_ga` | jedno wywołanie oceny z GA (kopie + kernel) |
| `fitness_h2d` | `GpuFitness` | kopia populacji host → GPU |
| `fitness_kernel` | `GpuFitness` | kernel fitness (do `cudaDeviceSynchronize`) |
| `fitness_d2h` | `GpuFitness` | kopia wyników GPU → host |
| `mpi_migrate` | `Island` | wysyłka i odbiór migrantów w generacji |
| `mpi_terminate` | `Island` | protokół zakończenia ([ADR 0009](adr/0009-zakonczenie.md)) |

Operatory hosta (selekcja, krzyżowanie, mutacja, elitaryzm) **nie mają** własnych zakresów: ich czas to
`ga_run` minus (`ga_evaluate` + `mpi_migrate`) albo kolumna `ops_time_s` w logu CSV.

## Nsight Systems pod MS-MPI

Jeden plik raportu na rank. MS-MPI ustawia w środowisku procesu zmienną `PMI_RANK`, a `nsys` rozwija `%q{ZMIENNA}`
w nazwie pliku (**do zweryfikowania**):

```powershell
mkdir results\prof
mpiexec -n 2 nsys profile -t cuda,nvtx --output results\prof\rep_%q{PMI_RANK} build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 4096 --generations 300 --seed 1 --log results\prof\run.csv
```

Jeśli `%q{PMI_RANK}` nie zadziała, uruchom jeden rank (`-n 1`) i profiluj osobno albo zacznij od pojedynczej wyspy
(`qmap_ga` działa też bez `mpiexec`). Na jednym komputerze obie rangi dzielą kartę, więc takie profile nadają się do
analizy hosta i kopii, nie do oceny przepustowości kernela. Profil klastra: `nsys` na każdym hoście osobno
(MPI nie jest tu śledzone, zob. ADR 0011).

Raporty tekstowe:

```powershell
nsys stats --report cuda_gpu_kern_sum,cuda_api_sum,nvtx_sum --format table results\prof\rep_0.nsys-rep
```

Jak czytać:

- `nvtx_sum`: czas i liczba wywołań zakresów. Porównaj `fitness_kernel` z `fitness_h2d` + `fitness_d2h` (czy kopie
  dominują nad obliczeniami?) oraz `ga_evaluate` z `ga_run` (ile pętli GA zajmuje GPU, a ile host). `mpi_migrate` i
  `mpi_terminate` pokazują koszt komunikacji widziany z hosta.
- `cuda_gpu_kern_sum`: czas kernela fitness, liczba wywołań, średnia i rozrzut (rozrzut między osobnikami = rozbieżność
  wątków w routerze).
- `cuda_api_sum`: `cudaMemcpy` (synchroniczne kopie z pamięci pageable), `cudaDeviceSynchronize` (czekanie hosta).
  Duży udział `cudaDeviceSynchronize` oznacza, że host czeka na GPU (nie nakłada pracy).
- Oś czasu w GUI (`nsys-ui`): luki między kernelami to czas operatorów hosta; nakładanie kopii i obliczeń w v1 nie
  występuje (wszystko synchroniczne).

## Nsight Compute

Wymaga dostępu do liczników GPU: [instalacja-windows.md, Krok 6](instalacja-windows.md). Profilujemy tylko kilka
wywołań kernela fitness (kernel jest powtarzany co generację; pełny zestaw sekcji spowalnia go wielokrotnie):

```powershell
ncu --set full -k regex:fitness -c 5 -o results\prof\fitness build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 4096 --generations 5 --seed 1
```

Sekcje do przejrzenia w `ncu-ui results\prof\fitness.ncu-rep`:

- **GPU Speed Of Light Throughput / Roofline**: czy kernel jest ograniczony pamięcią czy obliczeniami; v1 ma
  ruch nieregularny (tablice `dist`, `next_hop` w pamięci globalnej), więc spodziewamy się dominacji pamięci
  (hipoteza, do sprawdzenia).
- **Memory Workload Analysis**: współczynniki trafień L1/L2, sektory na żądanie (niskie wykorzystanie sektorów =
  dostęp niewspółosiowy).
- **Occupancy**: zajętość teoretyczna i osiągnięta; limit rejestrów i pamięci lokalnej (tablice `l2p` / `p2l`).
- **Warp State Statistics / Source Counters**: rozbieżność wątków i gorące linie w widoku źródła
  (dzięki `--generate-line-info`).

## Znane problemy na naszych komputerach (2026-11-25)

| Objaw | Przyczyna | Obejście |
|---|---|---|
| `nsys` przerywa z `boost::filesystem::status: Odmowa dostępu [system:5]: "C:\Users\<użytkownik>\AppData\Local\nvm\…"` | `PATH` zawiera dowiązanie nvm-windows (`C:\nvm4w\nodejs`) do profilu innego użytkownika. `nsys` uruchomiony jako `hpc` nie może go odczytać | przekazać `nsys` oczyszczony `PATH`: `-env PATH $P` w `mpiexec` albo `$env:PATH = $P` przed `nsys stats` (zmienna `$P` jest w kroku 7 kampanii) |
| `Executable '…\qmap_ga.exe' not found in current directory or standard search paths` (także przy pełnej ścieżce) | na AMG-NORTH `C:\hpc\HPC-quantum-transpilaton` to junction do `E:\mgr\sem2\HPC-quantum-transpilaton`, a `nsys` go nie rozwija; ścieżka względna też zawodzi | na AMG-NORTH podać prawdziwą ścieżkę `E:\…\qmap_ga.exe`, na pozostałych hostach pełną ścieżkę `C:\hpc\…`; dodatkowo `-wdir` w `mpiexec` |
| `WARNING: CPU sampling / context switches trace requires administrative privileges` | `nsys` działa na koncie `hpc` bez uprawnień administratora | nieszkodliwe: śledzimy tylko `cuda,nvtx` |
| `ncu` nie ma w katalogu głównym instalacji | jest tam tylko `ncu.bat`, a `ncu.exe` leży w `target\windows-desktop-win7-x64` | ścieżka przez `Get-ChildItem` (krok 7.4 kampanii) |
| `ncu --import … --page details/raw --csv` kończy się `==ERROR== Failed to handle input report: bad conversion` | błąd konwersji przy eksporcie tekstowym (najpewniej ustawienia regionalne); sam raport jest poprawny | odczyt w `ncu-ui` albo przez moduł Pythona `ncu_report` z `<Nsight Compute>\extras\python` (działa z Pythonem projektu przez `uv run`) |

Wyniki profilu V0: [wyniki.md](wyniki.md), sekcja „Iteracja I – profil V0”.

## Pierwsza wskazówka z testu dymnego (nie pomiar)

Test dymny: `qft_n12`, `--pop 4096`, RTX 5080, 2 rangi na jednej karcie. Średnio na generację: czas oceny
(`eval_time_s`) ≈ 0,35 ms, a operatory GA na hoście (`ops_time_s`) ≈ 0,68 ms. Przy małej populacji dominują więc
**operatory hosta**, nie GPU. To tylko wstępna obserwacja z logów CSV (dwie rangi współdzieliły kartę, jedna próba);
trzeba ją potwierdzić profilem `nsys` (w szczególności po dodaniu drobniejszych zakresów NVTX dla operatorów),
zanim cokolwiek zoptymalizujemy. Profil V0 z 2026-11-25 ją potwierdził: operatory hosta to 87–92 % czasu generacji
([wyniki.md](wyniki.md)).

## Strona Pythona

```powershell
uv run python -m cProfile -s cumtime -m qmap.validate --instance qft_n8 --routed out\qft_n8.qasm --layout out\layout.json --k 1
uv run py-spy record -o results\prof\validate.svg -- python -m qmap.baseline --instances qft_n8 --seeds 0
```

`py-spy` nie jest zależnością projektu; `uv run --with py-spy py-spy ...` dodaje go tylko na czas polecenia.

## Przebieg iteracji optymalizacji

1. **Zmierz**: ustalona komenda (instancja, `--pop`, `--seed`, `--generations`), profil `nsys` + log CSV.
2. **Wskaż wąskie gardło**: zakres NVTX / kernel o największym udziale; w razie potrzeby `ncu` dla kernela.
3. **Gałąź** `feature/perf-<opis>` (lub `fix/`), jedna zmiana na gałąź.
4. **Zmierz po zmianie** tą samą komendą na tym samym sprzęcie; porównaj `evals_per_s` i profil.
5. **Zapisz** w [wyniki.md](wyniki.md): wersja, komenda, przed/po, wniosek (także gdy zysku nie ma).
   Poprawność: `uv run pytest` i `ctest` po każdej zmianie; wynik `best` dla ustalonego ziarna nie może się pogorszyć
   bez uzasadnienia.
