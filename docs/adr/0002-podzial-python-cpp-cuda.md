# 0002. Podział Python / C++ / CUDA

- Status: Zaakceptowana
- Data: 2026-11-03

## Kontekst

Projekt łączy ekosystem Qiskit (generowanie obwodów, baseline SABRE, walidacja) z wysokowydajnym GA
na GPU i MPI. Trzeba rozdzielić odpowiedzialności tak, by pisać minimum kodu i mieć niezależną walidację.

## Rozważane opcje

- Całość w Pythonie (Qiskit + CuPy/mpi4py).
- Całość w C++/CUDA.
- Podział: Python dla Qiskit, C++ dla pętli GA, CUDA tylko dla fitness.

## Decyzja

- **Python + Qiskit:** generowanie instancji, baseline SABRE, walidacja, agregacja eksperymentów.
- **C++ (host):** pętla GA, operatory, logi.
- **CUDA:** wyłącznie fitness (liczba SWAP z routera zachłannego).
- **MPI:** asynchroniczna migracja najlepszego osobnika.
- **C++ jest jedynym źródłem prawdy o routingu** i emituje routed QASM; Python waliduje wynik niezależnie.

## Argumenty za i przeciw

### Podział (wybrany)
- Za: reużycie Qiskit tam, gdzie jest najsilniejszy; walidator niezależny od implementacji routera.
- Za: CUDA ogranicza się do jednego kernela, który da się profilować.
- Przeciw: dwa języki i format wymiany (QASM + JSON) do utrzymania.

### Całość w Pythonie
- Za: jedna technologia.
- Przeciw: słabsza kontrola nad wydajnością, kłopotliwy MPI/CUDA na Windows, trudniejsze profilowanie.

### Całość w C++/CUDA
- Za: jedna technologia wykonawcza.
- Przeciw: trzeba by własnoręcznie pisać walidację równoważności i baseline, co zmniejsza wiarygodność.

## Konsekwencje

- Dane wymieniamy plikami: QASM 2.0, JSON (urządzenie, layout).
- Rekonstrukcja routingu w Pythonie nie jest potrzebna (nie dublujemy routera); Python tylko sprawdza wynik.
