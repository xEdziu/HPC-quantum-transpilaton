# Dziennik decyzji

Chronologiczny rejestr decyzji projektowych, metodycznych i środowiskowych – materiał do sprawozdania
(sekcje 1.2, 2.6, 6). Decyzje architektoniczne mają pełne uzasadnienie w [ADR](adr/README.md); tutaj zapisujemy
każdą decyzję krótko: co, dlaczego, kto zdecydował, gdzie jest dowód.

| Data | Decyzja | Uzasadnienie | Źródło / dowód |
|---|---|---|---|
| 2026-11-03 | Natywny Windows 11 + MS-MPI (bez WSL) | wszystkie węzły na Windows; MS-MPI natywne z MSVC/CMake | [ADR 0001](adr/0001-windows-ms-mpi.md) |
| 2026-11-03 | Podział: Python/Qiskit (instancje, baseline, walidacja), C++ (GA, QASM), CUDA (fitness), MPI (migracja) | minimum nowego kodu, każda warstwa tam, gdzie jest najsilniejsza | [ADR 0002](adr/0002-podzial-python-cpp-cuda.md) |
| 2026-11-03 | Urządzenie IBM Falcon 27q (`FakeKolkataV2`), routing nieskierowany, bez szumu | realna topologia i baza bramek; kierunek CX poza zakresem | [ADR 0003](adr/0003-urzadzenie-falcon27.md) |
| 2026-11-03 | Instancje: QFT n ∈ {8,…,27} bez końcowych swapów, QAOA MaxCut p=1 na grafach 3-regularnych (n parzyste) | dwa kontrastowe profile obwodów; 27 niemożliwe dla grafu 3-regularnego | [ADR 0004](adr/0004-obwody-kanoniczne.md) |
| 2026-11-03 | Baseline SABRE z wyłączonymi ukrytymi optymalizacjami Qiskit (sam `SabreLayout`, parametry poziomu 3, przypięte wątki) | kontrolowane porównanie liczby SWAP | [ADR 0005](adr/0005-baseline-sabre.md), [determinizm-qiskit.md](determinizm-qiskit.md) |
| 2026-11-03 | Walidacja: CheckMap + test strukturalny + globalny statevector (k=3, tolerancja 1e-8); ten sam walidator dla GA i SABRE | wymaganie prowadzącego (globalny wektor stanu całego rozwiązania) | [ADR 0006](adr/0006-walidacja.md), [walidacja.md](walidacja.md) |
| 2026-11-03 | Zarządzanie Pythonem wyłącznie przez `uv` (lock-file) | identyczne wersje na 3 węzłach | zespół |
| 2026-11-03 | Wersja V0 celowo prosta (nie optymalna), bez sztucznych spowolnień; optymalizacje tylko po profilowaniu | sprawozdanie ma pokazać iteracje „profil → wąskie gardło → poprawka → zysk” | zespół, [wyniki.md](wyniki.md) |
| 2026-11-04 | Chromosom = permutacja 27 kubitów, fitness = SWAP zachłannego routera (najkrótsza ścieżka, remis → najniższy indeks), jedna procedura host/device | mała przestrzeń, deterministyczna ocena, ten sam kod na CPU i GPU | [ADR 0007](adr/0007-chromosom-router.md) |
| 2026-11-04 | CUDA 12.9 + VS 2022 Build Tools (nie VS 2026, nie CUDA 13) | zgłoszone błędy nvcc z MSVC 19.50; 12.9 obsługuje sm_86 i sm_120 przy starszym sterowniku | [instalacja-windows.md](instalacja-windows.md) |
| 2026-11-04 | Migracja asynchroniczna najlepszego osobnika; stała (100 generacji) lub adaptacyjna A/B (Mambrini, Sudholt); topologia all/ring | mała komunikacja, brak synchronizacji wysp | [ADR 0008](adr/0008-migracja.md) |
| 2026-11-04 | Protokół zakończenia bez `MPI_Cancel` (STOP → Alltoall liczników → odbiór reszty → Waitall) | brak zakleszczeń i niedostarczonych komunikatów | [ADR 0009](adr/0009-zakonczenie.md) |
| 2026-11-04 | TTT liczony od wyjścia z bariery, minimum po rangach; cele T_ε = ⌈(1+ε)·B⌉, ε ∈ {0,10; 0,05; 0,02}; przebiegi bez sukcesu cenzurowane | metryka główna; zegary hostów niesynchronizowane | [ADR 0010](adr/0010-ttt.md) |
| 2026-11-04 | Narzędzia profilowania: Nsight Systems + Nsight Compute + NVTX; bez NVVP/nvprof | NVVP/nvprof nie wspierają cc ≥ 7.5 | [ADR 0011](adr/0011-narzedzia-profilowania.md) |
| 2026-11-04 | Przed jakąkolwiek optymalizacją: pełna kampania pomiarowa iteracji I na 3 maszynach i sprawozdanie | metodyczność: decyzja o optymalizacji musi wynikać z pomiarów | zespół (wstrzymano pierwszą próbę optymalizacji po jednym profilu) |
| 2026-11-22 | Hosty klastra adresowane po IP (192.168.66.61 / .2 / .169), nie po nazwach | rozwiązywanie nazw nie działa w naszej sieci | test łączności 2026-11-22 |
| 2026-11-22 | Wspólne lokalne konto `hpc` (to samo hasło) na 3 komputerach, `smpd -d` uruchamiane jako `hpc` na każdym węźle | grupa robocza bez domeny: inaczej „Odmowa dostępu (errno 5)” | [instalacja-windows.md](instalacja-windows.md), Krok 7 |
| 2026-11-22 | `mpiexec` zawsze z `-genv MPICH_NETMASK 192.168.66.0/255.255.255.0` (nie `-env`) | wirtualne karty (Hyper-V, VirtualBox) i Wi-Fi powodowały zawieszenia; w MPMD `-env` dotyczy tylko pierwszego bloku | test łączności 2026-11-22 |
| 2026-11-22 | Laptop (RTX 5070) wyłącznie po kablu, Wi-Fi wyłączone na czas pomiarów | drugi adres w tej samej podsieci | test łączności 2026-11-22 |
| 2026-11-22 | `qmap_ga` sam tworzy katalogi wyjściowe | ranki zdalne piszą logi na własnych dyskach | commit `7379392` |
| 2026-11-22 | Kilka progów TTT (ε = 0,10/0,05/0,02) mierzonych w jednym przebiegu; przebieg kończy najostrzejszy próg | ok. 3× krótsza kampania; to zmiana metodyki pomiaru, nie wydajności programu | [ADR 0012](adr/0012-wiele-progow-ttt.md) |
| 2026-11-22 | Zakres kampanii TTT iteracji I: `qft_n12`, `qft_n20`, `qaoa_n16`, `qaoa_n24`; konfiguracje {5080}, {5080+3060}, {5080+5070}, {wszystkie}; 10 powtórzeń; limit 60 s | małe/średnie i duże instancje obu typów; budżet ok. 2,7 h | zespół |
| 2026-11-22 | Logi ranków zdalnych zbierane przez udział administracyjny (`\\<IP>\C$`) kontem `hpc` | bez nich brak przepustowości i efektywności per węzeł | zespół |
