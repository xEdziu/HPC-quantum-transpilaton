# Profil wersji V0 – materiał do sprawozdania

Ten dokument zbiera wszystko o profilowaniu wersji V0 (iteracja I) w formie gotowej do przeniesienia do sprawozdania:

- co i jak zmierzyliśmy;
- jakie są wykresy i tabele;
- jak je czytać;
- jakie wnioski wynikają z pomiarów;
- do których sekcji szablonu to trafia.

Pełne liczby: [wyniki.md](wyniki.md), sekcja „Iteracja I – profil V0”. Komendy: [kampania-iteracja-1.md](kampania-iteracja-1.md),
krok 7. Znane problemy narzędzi: [profilowanie.md](profilowanie.md).

## Najważniejsze w trzech zdaniach

1. W wersji V0 GPU liczy ocenę osobników tylko przez ok. 10 % czasu generacji. Pozostałe ok. 90 % to operatory
   algorytmu genetycznego (selekcja, krzyżowanie, mutacja) wykonywane jednowątkowo na CPU. **To jest wąskie gardło
   V0.**
2. Kopie danych między CPU a GPU zajmują 1–2 % czasu generacji, a komunikacja MPI między komputerami poniżej 0,2 %.
   Rozproszenie na 3 komputery prawie nic nie kosztuje komunikacyjnie.
3. Sam kernel oceny (fitness) jest ograniczony dostępem do pamięci podręcznej L1 (pamięć lokalna wątków i tablice
   w pamięci globalnej), a nie obliczeniami. Przyspieszanie go ma jednak sens dopiero po przyspieszeniu operatorów
   na CPU: nawet nieskończenie szybki kernel skróciłby generację na RTX 5080 najwyżej 1,09×.

## Co zmierzyliśmy (do sekcji 2.5 „Narzędzia profilowania”)

| Narzędzie | Wersja | Do czego |
|---|---|---|
| NVIDIA Nsight Systems (`nsys`) | 2025.1.3 | oś czasu całego programu: kiedy pracuje CPU, kiedy GPU, kopie, MPI; zakresy NVTX wstawione w kod |
| NVIDIA Nsight Compute (`ncu`) | 2025.2.1 | szczegółowa analiza jednego kernela: przepustowość, zajętość multiprocesorów, pamięć podręczna, przestoje |
| NVTX (biblioteka w CUDA 12.9) | – | nazwane zakresy w kodzie: `ga_run`, `ga_evaluate`, `fitness_h2d`, `fitness_kernel`, `fitness_d2h`, `mpi_migrate`, `mpi_terminate` |

Konfiguracja pomiaru:

- Instancja `qft_n12` (QFT na 12 kubitach, urządzenie Falcon 27q), ziarno 1.
- **Profil pojedynczej karty**: jedna wyspa GA na każdej z trzech kart osobno, populacja 262 144 osobników, 50
  generacji. Te same ustawienia na każdej karcie, więc karty można porównać bezpośrednio.
- **Profil klastra**: przebieg na 3 komputerach przez 20 s, migracja `adaptiveB`, topologia „wszyscy do wszystkich”,
  populacje 262 144 (RTX 5080), 131 072 (RTX 3060), 65 536 (RTX 5070 Laptop), czyli takie jak w kampanii TTT.
  Każdy rank ma osobny raport `nsys`.
- **Nsight Compute**: pełny zestaw sekcji (`--set full`) dla 3 wywołań kernela fitness na każdej karcie (262 144
  osobników = 1024 bloki × 256 wątków). Wartości w tabelach pochodzą z pierwszego wywołania.

Uwaga metodyczna: `ncu` na czas pomiaru blokuje zegary GPU i wielokrotnie uruchamia kernel, więc czasy kernela
w `ncu` są dłuższe niż w zwykłym przebiegu (RTX 5080: 3,50 ms w `ncu` i 3,13 ms w `nsys`). Do czasów służy `nsys`,
a `ncu` – do proporcji i przyczyn.

## Wykresy i tabele

Wszystkie powstają z zapisanych raportów jedną komendą (w oknie uv, w katalogu repozytorium):

```powershell
uv run python -m qmap.experiments prof-plot
```

Pliki trafiają do `results\plots`. Wykresy zastępują większość zrzutów ekranu z GUI z listy w
[kampania-iteracja-1.md](kampania-iteracja-1.md), krok 8. Z GUI zostają tylko dwa zrzuty, opisane na końcu.

### `prof_breakdown.png` + `prof_breakdown.csv` – podział czasu generacji (sekcja 3.3)

**Co pokazuje:** poziome słupki, jeden na każdy profil (3 karty osobno + 3 ranki przebiegu klastrowego). Długość
słupka to średni czas jednej generacji w ms. Kolory dzielą go na części:

- szary: operatory GA na CPU;
- niebieski: kopia populacji na GPU (H2D);
- pomarańczowy: kernel oceny;
- zielony: kopia wyników z GPU (D2H);
- żółty: pozostały narzut oceny;
- fioletowy: obsługa migracji MPI.

Przy słupku jest podpis z udziałem CPU w procentach.

**Jak czytać:** szary fragment dominuje na każdym słupku (85–92 % dla pojedynczej karty). Pomarańczowy kernel to
3,5 ms na RTX 5080, 10,3 ms na RTX 3060 i 7,5 ms na laptopie. Fioletowego (MPI) praktycznie nie widać.

**Proponowany podpis:** „Średni czas generacji w wersji V0 z podziałem na operatory GA (CPU), kopie i kernel oceny
(GPU) oraz obsługę migracji MPI; qft_n12, Nsight Systems.”

Liczby do tabeli w tekście (ms na generację, z zakresów NVTX; ten sam podział co na wykresie):

| Profil | generacja | operatory GA (CPU) | kopia H2D | kernel | kopia D2H | udział CPU |
|---|---:|---:|---:|---:|---:|---:|
| RTX 5080 (262 144) | 48,3 | 44,2 | 0,51 | 3,47 | 0,11 | 92 % |
| RTX 3060 (262 144) | 94,7 | 83,3 | 0,90 | 10,32 | 0,22 | 88 % |
| RTX 5070 Laptop (262 144) | 58,0 | 49,2 | 1,03 | 7,54 | 0,22 | 85 % |

(Logi CSV programu (`eval_time_s`, `ops_time_s`, [wyniki.md](wyniki.md)) dają 87–92 %, bo w tamtym
zestawieniu pominięto generację 0 (rozruch, m.in. jednorazowe wywołanie kernela trwające 85 ms na laptopie). Dokładne wartości ze
wszystkimi kolumnami są w `prof_breakdown.csv`. Wiersze trzech ranków przebiegu klastrowego
też są w tym pliku: udział CPU 72–90 %.)

### `prof_timeline.png` – oś czasu kilku generacji (sekcja 3.3; zamiast zrzutów nr 1 i 2)

**Co pokazuje:** trzy panele, po jednym na kartę, ze wspólną skalą czasu. Każdy panel to 4 kolejne generacje
(10–13, z pominięciem rozruchu). Wiersze:

- „CPU: GA operators”: odcinki, w których działa tylko CPU;
- „GPU: copy H2D”, „GPU: fitness kernel”, „GPU: copy D2H”: rzeczywista aktywność karty zarejestrowana przez CUPTI.

**Jak czytać:** na każdej karcie widać krótkie bloki pracy GPU i długie szare odcinki CPU. GPU stoi bezczynnie przez
większość czasu, a kopie i kernel nigdy się nie nakładają (w V0 wszystko jest synchroniczne). Ze wspólnej skali
widać też, że generacja na RTX 3060 trwa około dwa razy dłużej niż na RTX 5080, głównie przez wolniejszy CPU.

**Proponowany podpis:** „Oś czasu czterech generacji wersji V0 na każdej karcie (Nsight Systems): krótkie fazy
pracy GPU przedzielone długimi fazami operatorów GA na CPU.”

### `prof_3hosts.png` – przebieg na 3 komputerach (sekcja 5, koszt rozproszenia; zamiast zrzutu nr 4)

**Co pokazuje:**

- **Górny panel**: kernele oceny na każdym ranku w pierwszej sekundzie przebiegu. Jeden słupek odpowiada jednej
  generacji. Osie są wyrównane do startu pętli GA, bo zegary komputerów nie są zsynchronizowane, a start następuje
  zaraz po wspólnej barierze MPI.
- **Dolny panel**: skumulowana liczba migrantów wysłanych (linia ciągła) i odebranych (przerywana) przez każdy
  rank w czasie.

**Jak czytać:**

- Laptop (65 536 osobników) robi generacje najczęściej, RTX 3060 najrzadziej. Wyspy pracują w różnym tempie, co
  jest naturalne w asynchronicznym modelu wyspowym.
- Migracje następują przez cały przebieg.
- Z tabeli NVTX wynika, że obsługa migracji kosztuje łącznie 4–7 ms na 20 s przebiegu (poniżej 0,05 %), a
  zakończenie (`mpi_terminate`) 4–35 ms jednorazowo.

**Proponowany podpis:** „Przebieg na trzech komputerach: tempo generacji poszczególnych wysp oraz liczba wysłanych
i odebranych migrantów w czasie.”

### `prof_ncu.png` + `prof_ncu.csv` – analiza kernela w Nsight Compute (sekcja 3.3; zamiast zrzutów nr 5, 8, 10)

**Co pokazuje:** dla każdej karty słupki „procent maksimum” dla:

- przepustowości SM (obliczenia);
- przepustowości pamięci;
- osiągniętej zajętości multiprocesorów;
- aktywności wydawania instrukcji;
- trafień w pamięć podręczną L1.

`prof_ncu.csv` ma pełną listę metryk: rejestry, IPC, liczby instrukcji pamięci, najczęstsze przyczyny przestojów
warpów.

| Metryka | RTX 5080 | RTX 3060 | RTX 5070 Laptop |
|---|---:|---:|---:|
| przepustowość SM | 14,6 % | 20,3 % | 19,1 % |
| przepustowość pamięci | 91,8 % | 73,6 % | 94,8 % |
| zajętość osiągnięta (teoretyczna 100 %) | 85 % | 94 % | 90 % |
| aktywne wątki w warpie (z 32) | 19,3 | 19,3 | 19,3 |
| trafienia L1 / L2 | 90 / 99 % | 80 / 100 % | 92 / 100 % |
| główne przestoje | oczekiwanie na pamięć (`long_scoreboard`), przepełniona kolejka L1 (`lg_throttle`) | to samo | to samo |

**Jak czytać:**

- Pamięć jest wykorzystana prawie w 100 %, a jednostki obliczeniowe w 15–20 %. Kernel jest więc ograniczony
  pamięcią, a nie obliczeniami.
- Trafienia w cache są bardzo wysokie, więc nie chodzi o pamięć DRAM karty, tylko o sam potok pamięci L1.
- Przyczyny w kodzie:
  - każdy wątek trzyma swoje tablice mapowania (`l2p`, `p2l`) w pamięci lokalnej, która fizycznie leży w L1/L2:
    16,5 mln odczytów i 19,5 mln zapisów na wywołanie;
  - tablice odległości i następnego kroku (`dist`, `next_hop`) są czytane z pamięci globalnej zamiast ze
    współdzielonej.
- 19,3 z 32 aktywnych wątków w warpie oznacza rozbieżność wątków: różne osobniki wymagają różnej liczby SWAP-ów,
  więc pętla routera kończy się w różnym momencie.

**Proponowany podpis:** „Kernel oceny w Nsight Compute: wysokie wykorzystanie potoku pamięci przy niskim
wykorzystaniu jednostek obliczeniowych (kernel ograniczony pamięcią).”

## Wniosek do sekcji 3.3 (wąskie gardło i decyzja)

Gotowy akapit do przeredagowania:

> Profil Nsight Systems wersji V0 pokazuje, że operatory algorytmu genetycznego wykonywane jednowątkowo na CPU
> zajmują 85–92 % czasu generacji (72–90 % w przebiegu na trzech komputerach), a GPU przez ten czas pozostaje
> bezczynne. Kopie danych CPU↔GPU to 1–2 % czasu generacji, a obsługa migracji MPI – poniżej 0,2 %. Zgodnie z prawem
> Amdahla nawet nieskończenie szybka ocena na GPU skróciłaby generację na RTX 5080 najwyżej 1,09×, podczas gdy
> usunięcie kosztu operatorów na CPU dawałoby do 12×. Kernel oceny jest ograniczony potokiem pamięci (Nsight Compute:
> 74–95 % przepustowości pamięci przy 15–20 % przepustowości obliczeniowej) z powodu tablic w pamięci lokalnej
> wątków i odczytów tablic routera z pamięci globalnej. **Decyzja:** w iteracji II w pierwszej kolejności
> przyspieszamy operatory GA (najpierw dokładniejsze zakresy NVTX dla selekcji, krzyżowania i mutacji, potem
> zrównoleglenie lub przeniesienie na GPU), a optymalizacje kernela i kopii odkładamy na kolejne iteracje.

Do rejestru ADR / tabeli V→V+1 (sekcja 6):

| Wersja | Ograniczenie | Decyzja | Dowód |
|---|---|---|---|
| V0 → V1 | operatory GA na CPU: 85–92 % czasu generacji | przyspieszyć operatory GA (zrównoleglenie lub GPU) | `prof_breakdown.png`, `prof_timeline.png`, [dziennik-decyzji.md](dziennik-decyzji.md) (2026-11-25) |

## Zagrożenia trafności (do sekcji 3.3 lub 2.6)

- Profil wykonano dla jednej instancji (`qft_n12`). Dla dłuższych obwodów kernel trwa dłużej (spada przepustowość,
  zob. jakość względem SABRE w [wyniki.md](wyniki.md)), więc udział GPU może być nieco większy. Rząd wielkości
  wniosku się nie zmieni.
- Jeden przebieg na profil. Rozrzut czasu kernela między generacjami jest mały, z dwoma wyjątkami:
  - pojedyncze wywołanie 85 ms na laptopie;
  - przebieg klastrowy na RTX 3060, gdzie kernel dla połowy populacji trwał średnio dłużej niż w profilu
    pojedynczej karty. Przyczyna niewyjaśniona, opisana w [wyniki.md](wyniki.md).
- `nsys` działał bez uprawnień administratora, więc bez próbkowania CPU. Operatorów na CPU nie rozbijamy jeszcze na
  poszczególne funkcje; to pierwszy krok iteracji II.

## Dwa zrzuty z GUI (sekcja 2.5 – „zrzuty z narzędzi”)

Tych widoków nie da się sensownie odtworzyć wykresem. Raporty leżą na AMG-NORTH w
`E:\mgr\sem2\HPC-quantum-transpilaton\results\prof`.

Otwarcie Nsight Compute (zwykłe okno PowerShell):

```powershell
& 'C:\Program Files\NVIDIA Corporation\Nsight Compute 2025.2.1\host\windows-desktop-win7-x64\ncu-ui.exe' E:\mgr\sem2\HPC-quantum-transpilaton\results\prof\ncu_AMG-NORTH.ncu-rep
```

1. **Roofline** (`fig_ncu_roofline_rtx5080.png`):
   - zakładka **Details**;
   - rozwiń sekcję **GPU Speed Of Light Roofline Chart**;
   - zrzut samego wykresu (Win+Shift+S).

   Na wykresie kropka kernela leży daleko pod dachem obliczeniowym, po stronie ograniczenia pamięcią.
2. **Source** (`fig_ncu_source_rtx5080.png`):
   - zakładka **Source**;
   - u góry wybierz widok **Source and SASS** albo samo **Source**;
   - kliknij nagłówek kolumny **Warp Stall Sampling (All Samples)**, żeby posortować malejąco;
   - zrzut kilkunastu górnych linii.

   Najgorętsze linie są w pętli routera `count_swaps` (odczyty `dist`/`next_hop` i aktualizacja `l2p`/`p2l`).

Zapisz oba pliki w `results\figures`.

Opcjonalnie, na potrzeby samej sekcji 2.5, jeden zrzut osi czasu z Nsight Systems pokazujący, jak wygląda narzędzie:

```powershell
& 'C:\Program Files\NVIDIA Corporation\Nsight Systems 2025.1.3\host-windows-x64\nsys-ui.exe' E:\mgr\sem2\HPC-quantum-transpilaton\results\prof\nsys_AMG-NORTH.nsys-rep
```

1. Rozwiń wiersze **CUDA HW** i **NVTX**.
2. Zaznacz myszą fragment kilku generacji i wybierz **Zoom to selection**.
3. Zapisz zrzut jako `fig_nsys_timeline_rtx5080.png`.

Ten sam obraz w czytelniejszej formie daje `prof_timeline.png`.

## Pliki

| Plik | Gdzie | Zawartość |
|---|---|---|
| `prof_breakdown.png`, `prof_breakdown.csv` | `results/plots` | podział czasu generacji |
| `prof_timeline.png` | `results/plots` | oś czasu 4 generacji na każdej karcie |
| `prof_3hosts.png` | `results/plots` | tempo wysp i migracje na 3 komputerach |
| `prof_ncu.png`, `prof_ncu.csv` | `results/plots` | metryki kernela z Nsight Compute |
| `stats_*.csv` | `results/prof` | surowe zestawienia `nsys stats` (kernele, API CUDA, zakresy NVTX) |
| `*.nsys-rep`, `*.ncu-rep` | `results/prof` (AMG-NORTH) | raporty do otwarcia w GUI (duże, poza archiwum) |
