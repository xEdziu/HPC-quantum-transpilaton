# Kampania pomiarowa iteracji I (wersja V0)

Dokument operacyjny dla zespołu: dokładna, uporządkowana lista poleceń do wykonania **przed jakąkolwiek
optymalizacją**, z mapowaniem wyników na sekcje szablonu sprawozdania (1.2 środowisko, 2.x metodyka, 3.3 pomiary V0,
5.2 koszt dystrybucji, 6 decyzje). Protokół i zagrożenia dla ważności: [protokol-eksperymentow.md](protokol-eksperymentow.md);
definicja TTT: [ADR 0010](adr/0010-ttt.md), wiele progów w jednym przebiegu: [ADR 0012](adr/0012-wiele-progow-ttt.md);
profilowanie: [profilowanie.md](profilowanie.md); klaster: [instalacja-windows.md](instalacja-windows.md), Krok 7.

Kod objęty pomiarem jest wersją **V0** (naiwną): w trakcie kampanii nie zmieniamy ani algorytmu, ani wydajności
programu. Wszystkie polecenia uruchamiamy my; żaden z agentów ich nie uruchamia.

## Konwencje

- Wszystko z **okna PowerShell konta `hpc`** na komputerze rank 0 (RTX 5080, AMG-NORTH, 192.168.66.61), w katalogu
  `C:\hpc\HPC-quantum-transpilaton`. Hosty adresujemy po IP. `mpiexec` zawsze z `-genv MPICH_NETMASK $NM`.
- Zmienne używane we wszystkich krokach (wklej na początku sesji):

  ```powershell
  cd C:\hpc\HPC-quantum-transpilaton
  $NM = "192.168.66.0/255.255.255.0"
  $H3 = @("-hosts", "3", "192.168.66.61", "1", "192.168.66.2", "1", "192.168.66.222", "1")   # rank 0 = RTX 5080
  ```

  Użycie: `mpiexec -timeout 120 -genv MPICH_NETMASK $NM @H3 <program> ...`.
- `data\cluster.json` (nie jest w git): `copy data\cluster.example.json data\cluster.json`; wartości `pop` w nim
  (5080: 262144, 3060: 131072, 5070: 65536) to wartości kampanii; zmieniamy je tylko na podstawie kroku 2.
- Czasy trwania podane niżej są szacunkami budżetu, nie pomiarami.

## 0. Warunki wstępne (przed każdą sesją pomiarową)

1. **Ten sam commit na 3 komputerach**, zbudowany w Release i sprawdzony (`check-env.ps1` same `[ OK ]`):

   ```powershell
   git rev-parse HEAD
   mpiexec -timeout 60 -genv MPICH_NETMASK $NM @H3 git rev-parse HEAD     # trzy identyczne linie
   mpiexec -timeout 60 -genv MPICH_NETMASK $NM @H3 git status --short      # puste
   mpiexec -timeout 120 -genv MPICH_NETMASK $NM @H3 build\Release\qmap_mpi_tests.exe
   ```

   Hash commitu zapisujemy do sprawozdania (sekcja 1.2). Reguła zapory dla `qmap_mpi_bench.exe`: Krok 7 w
   [instalacja-windows.md](instalacja-windows.md).
2. **`smpd -d` jako `hpc` na wszystkich 3 komputerach** (łącznie z rank 0), okna zostają otwarte.
3. **Laptop (RTX 5070 Laptop, 192.168.66.222):** kabel Ethernet do wbudowanej karty (nie karty USB), Wi-Fi wyłączone (`Disable-NetAdapter -Name "Wi-Fi"
   -Confirm:$false`), zasilacz podłączony, tryb wydajności. Laptop pozostaje w **normalnym użyciu** przez właściciela
   (nie możemy tego zabronić), więc nie wymagamy „zamknięcia wszystkiego”; zamiast tego **dokumentujemy obciążenie tła**
   (punkt 4) i opisujemy je w sprawozdaniu jako zagrożenie trafności ([dziennik decyzji](dziennik-decyzji.md)).
   Na pozostałych dwóch komputerach zamykamy programy obciążające GPU/CPU.
4. **Ciągły log nvidia-smi przez całą kampanię na każdym hoście** (na laptopie obowiązkowy: pokazuje użycie GPU
   przez inne programy, spadki zegarów i powody dławienia). Na **każdym** komputerze, w osobnym oknie PowerShell
   (zostaje otwarte do końca kampanii; plik narasta ok. 1 wiersz na sekundę):

   ```powershell
   cd C:\hpc\HPC-quantum-transpilaton
   mkdir results\bench -Force | Out-Null
   nvidia-smi --query-gpu=timestamp,utilization.gpu,temperature.gpu,clocks.sm,power.draw,clocks_throttle_reasons.active --format=csv -lms 1000 -f "results\bench\nvsmi_campaign_$env:COMPUTERNAME.csv"
   ```

   Zakończenie na koniec kampanii: `Ctrl+C` w tym oknie. (Uruchomienie tego zdalnie przez `mpiexec` nie jest zalecane:
   `smpd` może zakończyć proces potomny razem z zadaniem; nie sprawdzaliśmy tego.) Jeśli sterownik nie zna nazwy
   `clocks_throttle_reasons.active`, użyj `clocks_event_reasons.active`.
5. Na czas kampanii: nic dużego w sieci LAN (aktualizacje, kopiowanie), karty nie współdzielone z innymi
   programami GPU, komputery nie przechodzą w uśpienie (`powercfg /change standby-timeout-ac 0`).
6. Lista kontrolna laptopa (HyperX_Zuber) przed każdą serią pomiarów. W kampanii TTT laptop liczył w sesji 1 ok. 3,06 M
   ocen/s, a w sesji 2 ok. 4,61 M ocen/s; przyczyną były najpewniej ustawienia zasilania, nie sieć.
   - podłączony zasilacz (`standby-timeout-ac` działa tylko na zasilaniu sieciowym);
   - tryb zasilania Windows „Najlepsza wydajność” (Ustawienia → System → Zasilanie);
   - w aplikacji producenta (jeśli jest) najwyższy profil wydajności, grafika dedykowana (nie hybrydowa ani oszczędna);
   - nikt na nim nie pracuje i nie działają inne ciężkie procesy; laptop otwarty lub na podstawce, żeby się nie przegrzewał;
   - po pierwszym przebiegu z laptopem: mediana `evals_per_s` w jego `run_r<rank>.csv` powinna wynosić ok. 4,5 M/s (dla qaoa_n16/qaoa_n24; dłuższe obwody liczą się wolniej, np. qft_n27 ok. 2,9 M/s);
     ok. 3 M/s oznacza tryb oszczędzania – popraw ustawienia przed dalszymi pomiarami.

## 1. Rysunki i tabela instancji (lokalnie)

```powershell
uv run python -m qmap.baseline          # tylko jeśli brakuje results\baseline\sabre.csv (kolumny SABRE w tabeli)
uv run python -m qmap.figures --out results\figures
```

Powstaje w `results\figures`: `topology_falcon27`, `circuit_qft5`, `circuit_qaoa6`, `routing_before_after` (PNG i PDF),
`instances.csv` / `instances.tex`, `device_falcon27.tex`. Rysunek routingu wymaga `build\Release\qmap_eval.exe`.
Sprawdź wzrokowo każdy plik (czytelność przy połowie szerokości strony). Sekcje: 1.2/2.x (urządzenie i instancje).

## 2. Pomiary pojedynczych węzłów (`bench-node.ps1`)

Na każdym komputerze skrypt opisuje sprzęt, zapisuje `nvidia-smi` co 500 ms i przemiata rozmiar populacji
(4096 … 4194304) na `qft_n12`; od rozmiaru, przy którym **jedna ocena (`eval_time_s`) trwa dłużej niż 1,0 s**,
większe populacje są pomijane (zabezpieczenie przed resetem sterownika przez TDR). Na końcu 120 s przebiegu ciągłego
przy `-SustainPop` (wykrycie dławienia termicznego). Czas jednego węzła: ok. 5–10 min. Zalecamy **kolejno, po jednym
węźle** (komputery są niezależne, więc równoległe uruchomienie jest możliwe, ale kolejność utrzymuje cichą sieć i
czytelne logi). Laptop: poproś właściciela o kilka minut bez obciążenia GPU; jeśli to niemożliwe, migawki
„top CPU” (`bench_<host>_top_cpu_*.txt`) i log nvidia-smi dokumentują obciążenie tła.

```powershell
# rank 0 (lokalnie)
.\scripts\bench-node.ps1 -Instance qft_n12 -OutDir results\bench -SustainPop 262144
# RTX 3060 (PS04)
mpiexec -timeout 1800 -genv MPICH_NETMASK $NM -hosts 1 192.168.66.2 1 powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-node.ps1 -Instance qft_n12 -OutDir results\bench -SustainPop 131072
# RTX 5070 Laptop (HyperX_Zuber)
mpiexec -timeout 1800 -genv MPICH_NETMASK $NM -hosts 1 192.168.66.222 1 powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-node.ps1 -Instance qft_n12 -OutDir results\bench -SustainPop 65536
# zebranie plików ze zdalnych hostów i wykresy
.\scripts\collect-results.ps1 -Cluster data\cluster.json -Path results\bench
uv run python -m qmap.experiments bench-plot --bench results\bench --out results\plots
```

Skrypt usuwa z własnego środowiska zmienne zadania MPI (`PMI_*`, `MPIEXEC_*`, `MPICH_*`, `MSMPI_*` poza ścieżkami
instalacji), żeby `qmap_ga` uruchomiony przez `mpiexec` działał jako osobna wyspa, a nie próbował dołączyć do zadania.
Wynik: `bench_<host>_node.json`, `bench_<host>_sweep.csv`, `bench_<host>_pop<P>_r0.csv`, `bench_<host>_sustain_r0.csv`,
`nvsmi_<host>.csv`, `bench_<host>_top_cpu_{start,end}.txt` i wykresy `bench_sweep.png`, `bench_nvsmi_<host>.png`.

**Decyzja po kroku 2:** jeśli w `bench_<host>_sweep.csv` rozmiar z `data\cluster.json` ma status `slow` albo przepustowość
dalej rośnie wyraźnie powyżej tego rozmiaru, rozważ zmianę `pop` w `cluster.json` (zapisz w dzienniku decyzji i
powtórz tylko krok 2 dla tego hosta; pozostałe kroki wykonuj dopiero po ustaleniu `pop`).
Sekcje: 1.2 (opis sprzętu, `node.json`), 3.3 (przepustowość V0 na kartę, dławienie), 5.2 (przepustowość względna kart
potrzebna do efektywności heterogenicznej).

## 3. Opóźnienie i przepustowość MPI (`qmap_mpi_bench`)

```powershell
mpiexec -timeout 300 -genv MPICH_NETMASK $NM @H3 build\Release\qmap_mpi_bench.exe --out results\bench\mpi_pingpong.csv
uv run python -m qmap.experiments bench-plot --bench results\bench --out results\plots
```

Rank 0 wymienia komunikaty z każdym innym rankiem (1 B … 1 MiB; połowa czasu pętli, mediana i minimum w µs; przepustowość
w MB/s = 10^6 B/s) oraz mierzy `MPI_Barrier` na 3 rankach (wiersz `peer_rank = -1`). Wynik: `mpi_pingpong.csv`,
`mpi_pingpong.png`. **Powtórz raz po zakończeniu kampanii** (krok 5) i porównaj (laptop w użyciu i obciążenie tła).
Sekcja: 5.2 (koszt komunikatu migracji (31 B: 27 genów + `int`) i bariery względem czasu generacji).

## 4. Pilotaż (wyznaczenie B i progów)

Cztery instancje kampanii (`qft_n12`, `qft_n20`, `qaoa_n16`, `qaoa_n24`), pełny klaster, bez celu, 3 powtórzenia po
300 s: 4 × 3 × 300 s ≈ 1 h.

```powershell
uv run python -m qmap.experiments plan --cluster data\cluster.json --instances qft_n12 qft_n20 qaoa_n16 qaoa_n24 --configs rtx5080+rtx5070+rtx3060 --target -1 --reps 3 --max-time 300 --out results/pilot --script pilot.ps1
.\pilot.ps1
.\scripts\collect-results.ps1 -Cluster data\cluster.json -Path results\pilot
uv run python -m qmap.experiments pilot --runs results/pilot --out results/pilot/targets.csv
```

Obejrzyj `results\pilot\targets.csv` (`instance,B,eps,target`). Jeśli B nadal wyraźnie się poprawia w ostatnich
sekundach pilotażu (`uv run python -m qmap.experiments plot --runs results/pilot --out results/plots --convergence
results/pilot/rtx5080+rtx5070+rtx3060/qft_n20/target-1/rep1`), wydłuż pilotaż. Zatwierdzony `targets.csv` zapisujemy
(kopia do `docs\`/sprawozdania). Sekcja: 2.x (poziomy celu T_ε = ⌈(1 + ε)·B⌉), 6 (decyzja o B).

## 5. Główna kampania TTT

Jeden przebieg na (konfiguracja, instancja, powtórzenie) ze wszystkimi trzema progami ε = 0,10 / 0,05 / 0,02
(`--target <T_0.10>,<T_0.05>,<T_0.02>`, koniec przy najostrzejszym; [ADR 0012](adr/0012-wiele-progow-ttt.md)). 4 konfiguracje ×
4 instancje × 10 powtórzeń = 160 przebiegów po najwyżej 120 s: najwyżej ok. 5,3 h plus start `mpiexec` (przebieg,
który osiągnie najostrzejszy próg, kończy się wcześniej). Limit 120 s wynika z pilotażu (zob. [wyniki.md](wyniki.md)); skrypt
jest wznawialny, więc kampanię można podzielić na kilka sesji. Ziarno = numer powtórzenia
(to samo dla każdej konfiguracji); powtórzenia są przeplatane w czasie.

```powershell
uv run python -m qmap.experiments plan --cluster data\cluster.json --targets-from results/pilot/targets.csv --instances qft_n12 qft_n20 qaoa_n16 qaoa_n24 --reps 10 --max-time 120 --out results/runs --script run_all.ps1
.\run_all.ps1                                   # wznawialny: pomija przebiegi z istniejącym summary.json
.\scripts\collect-results.ps1 -Cluster data\cluster.json -Path results\runs
uv run python -m qmap.experiments validate-runs --runs results/runs
uv run python -m qmap.experiments aggregate --runs results/runs --sabre results/baseline/sabre.csv --out results
uv run python -m qmap.experiments plot --runs results/runs --out results/plots --convergence results/runs/rtx5080+rtx5070+rtx3060/qft_n20/rep1
```

Przerwany `run_all.ps1` uruchamiamy ponownie tą samą komendą. Każdy `mpiexec` ma `-timeout` = limit przebiegu + 60 s
(tu 180 s): zawieszony przebieg (np. host zniknął z sieci) zostaje przerwany, nie zapisuje `summary.json` i wykona się
ponownie przy następnym uruchomieniu skryptu. Każdy przebieg z niezerowym kodem wyjścia trafia do
`results\runs\failed_runs.log` (do sprawozdania). Przed sesją wyłączamy usypianie laptopa
(`powercfg /change standby-timeout-ac 0`). Układ katalogów:
`results\runs\<konfiguracja>\<instancja>\rep<k>\` (`summary.json` z `targets` i `ttt_by_target`, `best.qasm`, `best.json`,
`run_r<rank>.csv`, `stdout.txt`). Wynik: `results\aggregate.csv` (częstość sukcesu, mediana z cenzurowaniem, kwartyle,
przepustowość, przyspieszenie, efektywność, na konfigurację, instancję i ε), `results\quality.csv`, wykresy ECDF TTT.
Wynik GA, który nie przechodzi walidacji, jest błędem, nie wynikiem (kolumny `validated` / `valid`).
Sekcje: 3.3 (TTT V0), 5.2 (przyspieszenie i efektywność dodawania węzłów, zbieżność), 2.x (zagrożenia).

## 6. Jakość względem SABRE (wszystkie 12 instancji)

Jeden przebieg 120 s na całym klastrze na instancję, ziarno 1, bez celu (`--target -1`): 12 × 120 s ≈ 25 min.

```powershell
$ALL = "qft_n8","qft_n12","qft_n16","qft_n20","qft_n24","qft_n27","qaoa_n8","qaoa_n12","qaoa_n16","qaoa_n20","qaoa_n24","qaoa_n26"
uv run python -m qmap.experiments plan --cluster data\cluster.json --instances @ALL --configs rtx5080+rtx5070+rtx3060 --target -1 --reps 1 --max-time 120 --out results/quality --script quality.ps1
.\quality.ps1
.\scripts\collect-results.ps1 -Cluster data\cluster.json -Path results\quality
uv run python -m qmap.experiments validate-runs --runs results/quality
uv run python -m qmap.experiments aggregate --runs results/quality --sabre results/baseline/sabre.csv --out results/quality-agg
```

Tabela jakości: `results\quality-agg\quality.csv` (najlepszy SWAP GA vs minimum i mediana SABRE, na instancję). Sekcja: 3.3.
(Uwaga: `@ALL` w PowerShellu rozwija tablicę do osobnych argumentów.)

## 7. Profilowanie (Nsight Systems, Nsight Compute)

Kroki 7.1–7.3 i 7.5 uruchamiamy z okna `hpc` na AMG-NORTH. Komendy sprawdzono 2026-11-25 (Nsight Systems 2025.1.3,
Nsight Compute 2025.2.1 na wszystkich hostach). Znane problemy i obejścia: [profilowanie.md](profilowanie.md). Przed
krokami ustaw zmienne:

```powershell
$NM = "192.168.66.0/255.255.255.0"
$NSYS = 'C:\Program Files\NVIDIA Corporation\Nsight Systems 2025.1.3\target-windows-x64\nsys.exe'
$P = ($env:PATH -split ';' | Where-Object { $_ -and $(try { [void](Get-ChildItem -LiteralPath $_ -ErrorAction Stop | Select-Object -First 1); $true } catch { $false }) }) -join ';'
$WD = 'C:\hpc\HPC-quantum-transpilaton'
$EXE = 'C:\hpc\HPC-quantum-transpilaton\build\Release\qmap_ga.exe'
$EXE_L = 'E:\mgr\sem2\HPC-quantum-transpilaton\build\Release\qmap_ga.exe'
$G = @("--instance", "data\instances\qft_n12.qasm", "--device", "data\devices\falcon27.json", "--pop", "262144", "--generations", "50", "--seed", "1")
```

Co robią te zmienne:

- `$P` to `PATH` bez katalogów niedostępnych dla konta `hpc`. Bez tego `nsys` zgłasza błąd `boost::filesystem`
  przez dowiązanie nvm.
- `$EXE_L` to prawdziwa ścieżka na AMG-NORTH. Przez junction `C:\hpc` `nsys` zgłasza „not found”.
- `-wdir` w komendach ustala katalog roboczy.

Sprawdzenie, że Nsight jest na każdym hoście:
`mpiexec -timeout 60 -genv MPICH_NETMASK $NM -hosts 3 192.168.66.61 1 192.168.66.2 1 192.168.66.222 1 cmd /c dir /b
"C:\Program Files\NVIDIA Corporation"`.

Wszystkie raporty trafiają do `results\prof` na dysku danego hosta (zbieramy w punkcie 7.5).

**7.1 `nsys` na każdej karcie osobno (jedna wyspa, ta sama komenda).** `--pop 262144 --generations 50` na wszystkich
trzech kartach, żeby profile były porównywalne (jeśli krok 2 oznaczył ten rozmiar jako `slow` na danej karcie, użyj
największego rozmiaru bez `slow` i zapisz to). Nazwa raportu zawiera nazwę hosta:

```powershell
mkdir results\prof -Force | Out-Null
mpiexec -timeout 300 -genv MPICH_NETMASK $NM -wdir $WD -env PATH $P -hosts 1 192.168.66.61 1 $NSYS profile -t cuda,nvtx --force-overwrite true --output results\prof\nsys_AMG-NORTH $EXE_L @G --log results\prof\run_AMG-NORTH.csv
mpiexec -timeout 300 -genv MPICH_NETMASK $NM -wdir $WD -hosts 1 192.168.66.2 1 $NSYS profile -t cuda,nvtx --force-overwrite true --output results\prof\nsys_PS04 $EXE @G --log results\prof\run_PS04.csv
mpiexec -timeout 300 -genv MPICH_NETMASK $NM -wdir $WD -hosts 1 192.168.66.222 1 $NSYS profile -t cuda,nvtx --force-overwrite true --output results\prof\nsys_HyperX_Zuber $EXE @G --log results\prof\run_HyperX_Zuber.csv
```

**7.2 `nsys` przebiegu na 3 hostach (po jednym raporcie na rank).** Nazwa z rangą przez `%q{PMI_RANK}` (w apostrofach,
żeby PowerShell nie ruszył klamr); 20 s, `adaptiveB`, każdy host ze swoim `pop`:

```powershell
$A = @("--instance", "data\instances\qft_n12.qasm", "--device", "data\devices\falcon27.json", "--max-time", "20", "--generations", "100000", "--seed", "1", "--migration", "adaptiveB", "--topology", "all")
mpiexec -timeout 300 -genv MPICH_NETMASK $NM -wdir $WD -n 1 -host 192.168.66.61 -env PATH $P $NSYS profile -t cuda,nvtx --force-overwrite true --output 'results\prof\nsys3_%q{PMI_RANK}' $EXE_L @A --pop 262144 --log results\prof\run3.csv : -n 1 -host 192.168.66.2 $NSYS profile -t cuda,nvtx --force-overwrite true --output 'results\prof\nsys3_%q{PMI_RANK}' $EXE @A --pop 131072 --log results\prof\run3.csv : -n 1 -host 192.168.66.222 $NSYS profile -t cuda,nvtx --force-overwrite true --output 'results\prof\nsys3_%q{PMI_RANK}' $EXE @A --pop 65536 --log results\prof\run3.csv
```

`%q{PMI_RANK}` działa (raporty `nsys3_0`, `nsys3_1`, `nsys3_2`, każdy na dysku swojego hosta).

**7.3 Raporty tekstowe** (po zebraniu plików w punkcie 7.5; w oknie `hpc` na AMG-NORTH, w katalogu
`E:\mgr\sem2\HPC-quantum-transpilaton`, po `$env:PATH = $P`):

```powershell
foreach ($f in Get-ChildItem results\prof\*.nsys-rep) {
    & $NSYS stats --report cuda_gpu_kern_sum,cuda_api_sum,nvtx_sum --format csv --force-export true --output "results\prof\stats_$($f.BaseName)" $f.FullName
}
```

**7.4 `ncu` na każdej karcie.** Wymaga dostępu do liczników GPU
([instalacja-windows.md, Krok 6](instalacja-windows.md)) **na każdym komputerze**. Profilujemy 3 wywołania kernela
fitness (`--generations 3`; pełny zestaw sekcji spowalnia kernel wielokrotnie). Najprościej uruchomić lokalnie na każdym
hoście, w oknie **Administratora** (liczniki GPU). Na AMG-NORTH pracuj w `E:\mgr\sem2\HPC-quantum-transpilaton`, na
pozostałych hostach w `C:\hpc\HPC-quantum-transpilaton`:

```powershell
$NCU = (Get-ChildItem 'C:\Program Files\NVIDIA Corporation\Nsight Compute*\target\windows-desktop-win7-x64\ncu.exe' | Select-Object -Last 1).FullName
& $NCU --set full -k regex:fitness -c 3 --force-overwrite -o "results\prof\ncu_$env:COMPUTERNAME" build\Release\qmap_ga.exe --instance data\instances\qft_n12.qasm --device data\devices\falcon27.json --pop 262144 --generations 3 --seed 1
```

(`ncu.exe` leży w `target\windows-desktop-win7-x64`; w katalogu głównym instalacji jest tylko `ncu.bat`.
Zdalnie: ta sama komenda z pełną ścieżką `ncu.exe` po `mpiexec -hosts 1 <ip> 1`; nazwa raportu `ncu_<host>` ręcznie.)

**7.5 Zebranie raportów** (skrypt `collect-results.ps1` kopiuje tylko logi, więc profile ręcznie; duże pliki):

```powershell
foreach ($ip in "192.168.66.2", "192.168.66.222") {
    robocopy "\\$ip\C$\hpc\HPC-quantum-transpilaton\results\prof" results\prof *.nsys-rep *.ncu-rep run*.csv stats_*.csv /NJH /NJS /NP
}
```

Wszystkie profile powstają dla wersji V0; żadnej optymalizacji jeszcze nie wprowadzamy.

## 8. Zrzuty ekranu do zrobienia ręcznie (GUI)

Zrzuty nr 1–5, 8 i 10 zastępują wykresy z `uv run python -m qmap.experiments prof-plot` (`results/plots/prof_*.png`,
opis: [profil-v0-dla-sprawozdania.md](profil-v0-dla-sprawozdania.md)); ręcznie zostają nr 7 (Memory Chart) i 9 (Source) oraz przycięta sekcja Speed Of Light. Nr 6 (Roofline) odpada:
kernel nie wykonuje operacji zmiennoprzecinkowych, więc Roofline nie ma punktu kernela.

Zrzuty robimy z raportów `*.nsys-rep` / `*.ncu-rep` (otwórz w `nsys-ui` / `ncu-ui`), PNG z paska narzędzi lub
Win+Shift+S, bez zbędnych paneli, motyw jasny (do druku), nazwa pliku `fig_<opis>.png` w `results\figures`.

| # | Narzędzie i widok | Powiększenie / ustawienia | Co ma być widać | Sekcja |
|---|---|---|---|---|
| 1 | Nsight Systems, raport `nsys_AMG-NORTH`, oś czasu: wiersze CUDA HW (kernele, Memcpy), CUDA API, NVTX | 3–5 kolejnych generacji (zaznacz zakres myszą, `Zoom to selection`), z rozwiniętym wierszem NVTX | zakresy `ga_run` > `ga_evaluate` > `fitness_h2d` / `fitness_kernel` / `fitness_d2h`; **luki bez pracy GPU** między generacjami (operatory hosta), brak nakładania kopii i obliczeń | 3.3 (profil V0, wąskie gardło) |
| 2 | to samo dla `nsys_PS04` i `nsys_HyperX_Zuber` | ta sama liczba generacji | porównanie proporcji kernel / kopie / luka na trzech kartach | 3.3, 5.2 |
| 3 | Nsight Systems, widok `Events View` albo zakładka **NVTX Range Summary** (lub `stats_*nvtx_sum.csv` jako tabela) | cały raport | czas całkowity i liczba wywołań każdego zakresu; udział `fitness_kernel` względem `fitness_h2d` + `fitness_d2h` i `ga_evaluate` względem `ga_run` | 3.3 |
| 4 | Nsight Systems, raport `nsys3_*` (rank 0 i rank 1 obok siebie: `Add report to timeline` / dwa okna) | kilka sekund; w wierszu NVTX zakres `mpi_migrate` | zakresy `mpi_migrate` (krótkie, co generację) i ewentualny `mpi_terminate` na końcu; różne tempo generacji kart | 5.2 (koszt dystrybucji) |
| 5 | Nsight Compute, `ncu_AMG-NORTH`, sekcja **GPU Speed Of Light Throughput** | pierwszy z 3 profilowanych kerneli | procent przepustowości obliczeń i pamięci (które ograniczenie dominuje) | 3.3 |
| 6 | Nsight Compute, **GPU Speed Of Light Roofline Chart** | domyślne | położenie kernela względem dachów (FP32 / pamięć), skala log-log | 3.3 |
| 7 | Nsight Compute, **Memory Workload Analysis** (Memory Chart) | rozwinięty wykres pamięci | ruch L1/L2/DRAM, współczynniki trafień, sektory na żądanie (dostęp nieregularny do `dist` / `next_hop`) | 3.3 |
| 8 | Nsight Compute, **Occupancy** (+ **Launch Statistics**) | domyślne | zajętość teoretyczna vs osiągnięta, limit rejestrów / pamięci lokalnej (`l2p`, `p2l`) | 3.3 |
| 9 | Nsight Compute, **Source** (strona *Source*, widok CUDA-C / SASS) | sortuj po `Warp Stall Sampling` lub `Instructions Executed`; `-lineinfo` jest włączone | gorące linie routera (`count_swaps`) i pętli po parach 2q | 3.3 (wąskie gardło → kandydat do optymalizacji) |
| 10 | to samo (pkt 5, 7, 8) dla `ncu_PS04` i `ncu_HyperX_Zuber` | | różnice między sm_86 i sm_120 (dla porównania; wystarczy po jednym zrzucie SOL na kartę) | 5.2 |
| 11 | Wykres `bench_nvsmi_<host>.png` (generowany, nie zrzut) | | zegar SM, temperatura i moc w przebiegu ciągłym; dławienie laptopa | 3.3 / 5.2 |

## 9. Co trafia do sprawozdania

| Artefakt | Plik | Sekcja szablonu |
|---|---|---|
| Opis węzłów: CPU, RAM, GPU, VRAM, sterownik, PCIe, NIC, CUDA | `results\bench\bench_<host>_node.json` | 1.2 |
| Obciążenie tła (top CPU), log kampanii `nvidia-smi` | `bench_<host>_top_cpu_*.txt`, `nvsmi_campaign_<host>.csv` | 1.2, 2.x (zagrożenia) |
| Topologia Falcon 27q, tabela urządzenia | `topology_falcon27.pdf`, `device_falcon27.tex` | 1.2 / 2.x |
| Obwody QFT i QAOA, tabela instancji | `circuit_qft5.pdf`, `circuit_qaoa6.pdf`, `instances.tex` | 2.x |
| Routing przed/po (SWAP = 3 CX) | `routing_before_after.pdf` | 2.x |
| Metodyka TTT, progi ε, cenzurowanie | [ADR 0010](adr/0010-ttt.md), [ADR 0012](adr/0012-wiele-progow-ttt.md), `results\pilot\targets.csv` | 2.x |
| Przepustowość na kartę vs populacja, dławienie | `bench_sweep.png`, `bench_nvsmi_<host>.png`, `bench_<host>_sweep.csv` | 3.3 |
| TTT V0 (ECDF, mediany, kwartyle, sukces w limicie) | `results\aggregate.csv`, `results\plots\ttt_ecdf_*.png` | 3.3 |
| Jakość względem SABRE | `results\quality-agg\quality.csv` | 3.3 |
| Zbieżność przykładowego przebiegu | `results\plots\convergence_*.png` | 3.3 |
| Profil V0 (nsys, ncu, zrzuty z punktu 8) | `results\prof\*`, `fig_*.png` | 3.3 |
| Przyspieszenie i efektywność heterogeniczna | kolumny `speedup`, `efficiency` w `aggregate.csv` | 5.2 |
| Opóźnienie / przepustowość MPI, koszt migracji, `mpi_migrate` w nsys3 | `mpi_pingpong.csv/.png`, zrzut nr 4 | 5.2 |
| Decyzje (środowisko, metodyka, `pop`, limity) | [dziennik-decyzji.md](dziennik-decyzji.md), [ADR](adr/README.md) | 6 |
| Wersja kodu (hash commitu), komendy kampanii | wynik `git rev-parse HEAD`, ten dokument | 1.2, 2.x |

## Czas i kolejność (budżet)

| Krok | Szacunek |
|---|---|
| 0, 1 | 30 min |
| 2 (3 węzły po kolei) | 30 min |
| 3 | 5 min |
| 4 pilotaż | 1 h |
| 5 kampania TTT | 2,7 h + walidacja i agregacja |
| 6 jakość | 25 min |
| 7 profilowanie | 1 h |
| powtórzenie kroku 3 | 5 min |

## 10. Badanie dodatkowe: interwał i topologia migracji (noc 2026-11-25/26)

**Cel.** Kampania TTT (punkt 3) używała jednego wariantu migracji: `adaptiveB` z topologią `all`. Na `qaoa_n16`
szybka wyspa z małą populacją mogła narzucać swoje rozwiązanie pozostałym ([wyniki.md](wyniki.md)). Sprawdzamy, czy
inny interwał lub topologia migracji zmienia TTT w konfiguracji 3-hostowej. To badanie parametrów wersji V0, bez
zmian w kodzie, więc nie łamie zasady „najpierw sprawozdanie, potem optymalizacja”.

**Warianty** (konfiguracja `rtx5080+rtx5070+rtx3060`, te same 4 instancje, progi z pilotażu, populacje
262 144 / 65 536 / 131 072, 120 s, ziarna 1–10):

| Wariant | `--migration` | `--topology` | Katalog |
|---|---|---|---|
| bazowy (już zmierzony w kampanii) | `adaptiveB` | `all` | `results\runs\rtx5080+rtx5070+rtx3060` |
| stały interwał | `fixed:100` | `all` | `results\runs-migration\fixed100-all` |
| adaptacyjny A | `adaptiveA` | `all` | `results\runs-migration\adaptiveA-all` |
| pierścień | `adaptiveB` | `ring` | `results\runs-migration\adaptiveB-ring` |

Te same ziarna co w kampanii dają porównanie sparowane z wariantem bazowym. Warianty są przeplecione przebieg po
przebiegu, żeby zmiany w czasie (np. temperatura i zasilanie laptopa) nie faworyzowały żadnego z nich. Wariant bazowy
mierzono jednak w innej sesji (2026-11-22–24), co jest zagrożeniem trafności przy porównaniu z nim.

**Uruchomienie** (okno `hpc` na AMG-NORTH; laptop podłączony do zasilacza, tryb wydajności, lista kontrolna z
punktu 0). Skrypt `night_migration.ps1` (120 przebiegów, najwyżej ok. 4,5 h) wygenerowano trzema wywołaniami
`plan` (`--migration`, `--topology`, `--out` jak w tabeli) i przepleciono:

```powershell
cd C:\hpc\HPC-quantum-transpilaton
powershell -ExecutionPolicy Bypass -File .\night_migration.ps1
```

Skrypt można wznowić: przebiegi z `summary.json` są pomijane. Nieudane przebiegi trafiają do
`results\runs-migration\<wariant>\failed_runs.log`. Po zakończeniu uruchom `validate-runs` dla każdego katalogu
wariantu. Porównanie TTT z wariantem bazowym (ECDF, mediana, częstość sukcesu) dodamy do `qmap.experiments`.
