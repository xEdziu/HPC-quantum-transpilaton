#pragma once
// MPI island layer: asynchronous migration of the best individual between GA islands
// (one MPI rank = one island). Host only; mpi.h is included in island_mpi.cpp and nowhere else.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "qmap/ga.hpp"
#include "qmap/migration.hpp"

namespace qmap {

// MPI_Init_thread(FUNNELED) in the constructor, MPI_Finalize in the destructor.
class MpiSession {
public:
    MpiSession(int& argc, char**& argv);
    ~MpiSession();
    MpiSession(const MpiSession&) = delete;
    MpiSession& operator=(const MpiSession&) = delete;

    int rank() const { return rank_; }
    int size() const { return size_; }

    // MPI_Abort(MPI_COMM_WORLD, code): ends every rank of the job.
    [[noreturn]] static void abort(int code);

private:
    int rank_ = 0, size_ = 1;
};

// splitmix64(seed ^ (golden_ratio * (rank + 1))): per-rank GaParams.seed.
uint64_t rank_seed(uint64_t seed, int rank);

struct IslandStats {
    long long migrants_sent = 0;      // migrant messages posted (STOP messages not counted)
    long long migrants_received = 0;  // migrant messages received, including those drained at termination
    int interval = 0;                 // current migration interval tau (generations)
};

// Outcome over all ranks. best_* are valid on every rank; ttt_s, evals and time_s only on rank 0.
struct GlobalResult {
    int best_fitness = 0;
    int best_rank = 0;            // rank that owns the global best (lowest rank on ties)
    std::vector<uint8_t> best;    // its n_phys genes
    double ttt_s = -1;            // min over ranks of the local ttt_s; < 0 if no rank reached the target
    long long evals = 0;          // sum over ranks
    double time_s = 0;            // max over ranks of the local total time
};

class Island {
public:
    // max_generations caps the adaptive interval. The optional callbacks wrap the per-generation
    // MPI work and the termination protocol (NVTX ranges in qmap_ga, so this library stays CUDA-free).
    Island(int n_phys, Topology topology, MigrationPolicy migration, int max_generations,
           std::function<void(const char*)> range_push = {}, std::function<void()> range_pop = {});
    ~Island();
    Island(const Island&) = delete;
    Island& operator=(const Island&) = delete;

    int rank() const;
    int size() const;

    // MPI_Barrier; call right before run_ga so that every rank's local clock starts together.
    void synchronize_start();

    // Hook for run_ga: sends/receives migrants. Returns false after a remote STOP.
    IslandHook hook();

    // Termination protocol (collective: every rank must call it exactly once after run_ga, with
    // the GaResult of its own run). A "target" rank first sends STOP to all others; then counts are
    // exchanged, all in-flight messages are drained and awaited, and the results are reduced.
    // Rewrites local.stop_reason "external" to "remote_stop". See docs/adr/0009-zakonczenie.md.
    GlobalResult finish(GaResult& local);

    const IslandStats& stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qmap
