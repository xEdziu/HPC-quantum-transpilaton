// Multi-rank island tests (CPU evaluator, no GPU). Run under mpiexec with >= 2 ranks (CTest uses 3):
//   mpiexec -n 3 qmap_mpi_tests.exe
// Every case is collective; the exit code is non-zero on every rank iff a CHECK failed on any rank.
#include <mpi.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "qmap/device.hpp"
#include "qmap/ga.hpp"
#include "qmap/island_mpi.hpp"
#include "qmap/migration.hpp"
#include "qmap/qasm_io.hpp"
#include "qmap/router.hpp"

#ifndef QMAP_DATA_DIR
#error "QMAP_DATA_DIR must point at the repository data directory"
#endif

namespace fs = std::filesystem;
using namespace qmap;

static int g_failures = 0;
static int g_rank = 0;
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::fprintf(stderr, "rank %d: CHECK failed %s:%d: %s\n", g_rank, __FILE__, __LINE__, #cond); \
        }                                                                                        \
    } while (0)

struct CpuProblem {
    Device dev;
    std::vector<uint8_t> pairs;
    int n_logical;
};

static CpuProblem load_problem(const std::string& instance) {
    CpuProblem p;
    p.dev = load_device((fs::path(QMAP_DATA_DIR) / "devices/falcon27.json").string());
    const Circuit c = parse_qasm_file((fs::path(QMAP_DATA_DIR) / ("instances/" + instance + ".qasm")).string());
    p.pairs = two_qubit_pairs(c);
    p.n_logical = c.n_qubits;
    return p;
}

static int fitness_of(const CpuProblem& p, const uint8_t* genes) {
    const int n = p.dev.n_phys;
    std::vector<uint8_t> l2p(genes, genes + p.n_logical), p2l(static_cast<size_t>(n), kEmpty);
    for (int i = 0; i < p.n_logical; ++i) p2l[genes[i]] = static_cast<uint8_t>(i);
    return count_swaps(p.pairs.data(), static_cast<int>(p.pairs.size() / 2), l2p.data(), p2l.data(), p.dev.dist.data(),
                       p.dev.next_hop.data(), n);
}

static Evaluator cpu_evaluator(const CpuProblem& p) {
    return [&p](const uint8_t* pop, int pop_size, int* out) {
        for (int k = 0; k < pop_size; ++k) out[k] = fitness_of(p, pop + static_cast<size_t>(k) * p.dev.n_phys);
    };
}

struct Outcome {
    GaResult local;
    GlobalResult global;
    IslandStats stats;
};

static Outcome run_island(const CpuProblem& p, GaParams params, Topology topology, MigrationPolicy migration) {
    params.seed = rank_seed(params.seed, g_rank);
    Island island(p.dev.n_phys, topology, migration, params.max_generations);
    island.synchronize_start();
    Outcome o;
    o.local = run_ga(params, p.dev.n_phys, cpu_evaluator(p), {}, island.hook());
    o.global = island.finish(o.local);
    o.stats = island.stats();
    return o;
}

static long long sum_over_ranks(long long v) {
    long long total = 0;
    MPI_Allreduce(&v, &total, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    return total;
}

static int min_over_ranks(int v) {
    int m = 0;
    MPI_Allreduce(&v, &m, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    return m;
}

static int max_over_ranks(int v) {
    int m = 0;
    MPI_Allreduce(&v, &m, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    return m;
}

// Nothing may be lost or left unreceived: every migrant posted is received by someone.
static void check_message_balance(const Outcome& o) {
    CHECK(sum_over_ranks(o.stats.migrants_sent) == sum_over_ranks(o.stats.migrants_received));
}

static void check_global_agreement(const CpuProblem& p, const Outcome& o) {
    CHECK(min_over_ranks(o.global.best_fitness) == max_over_ranks(o.global.best_fitness));
    CHECK(o.global.best_fitness <= o.local.best_fitness);
    CHECK(o.global.best_rank >= 0 && o.global.best_fitness == fitness_of(p, o.global.best.data()));
    CHECK(min_over_ranks(o.global.best_rank) == max_over_ranks(o.global.best_rank));
    if (g_rank == o.global.best_rank) CHECK(o.local.best_fitness == o.global.best_fitness);
}

static GaParams base_params() {
    GaParams params;
    params.pop_size = 64;
    params.max_generations = 50;
    params.max_time_s = 120;
    params.seed = 1;
    return params;
}

static void test_fixed_all(const CpuProblem& p) {
    int size = 1;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MigrationPolicy fixed;
    fixed.interval = 5;
    const Outcome o = run_island(p, base_params(), Topology::All, fixed);
    CHECK(o.local.stop_reason == "max_generations");
    CHECK(o.local.generations == 50);
    CHECK(o.stats.migrants_sent == 10LL * (size - 1));  // generations 5, 10, ..., 50 to every other rank
    CHECK(o.stats.migrants_received > 0);
    check_message_balance(o);
    check_global_agreement(p, o);
    if (g_rank == 0) {
        CHECK(o.global.evals == static_cast<long long>(size) * 51 * 64);
        CHECK(o.global.ttt_s < 0);
        CHECK(o.global.time_s > 0);
    }
}

// Rank 0 hits a (generous) target at once; the others have no target and must be stopped remotely.
static void test_remote_stop(const CpuProblem& p) {
    GaParams params = base_params();
    params.max_generations = 100000;
    params.targets = {1000000, 999999};  // same list on every rank; only rank 0 stops on it
    if (g_rank == 0) params.target = 999999;
    MigrationPolicy fixed;
    fixed.interval = 5;
    const Outcome o = run_island(p, params, Topology::All, fixed);
    if (g_rank == 0) {
        CHECK(o.local.stop_reason == "target");
        CHECK(o.global.ttt_s >= 0);
        CHECK(o.global.ttt_by_target.size() == 2);
        CHECK(o.global.ttt_by_target[0] >= 0 && o.global.ttt_by_target[0] <= o.global.ttt_by_target[1]);
    } else {
        CHECK(o.local.stop_reason == "remote_stop");
        CHECK(o.local.generations < 100000);
    }
    check_message_balance(o);
    check_global_agreement(p, o);
}

static void test_ring_adaptive(const CpuProblem& p, Scheme scheme) {
    GaParams params = base_params();
    params.max_generations = 60;
    MigrationPolicy adaptive;
    adaptive.scheme = scheme;
    const Outcome o = run_island(p, params, Topology::Ring, adaptive);
    CHECK(o.local.stop_reason == "max_generations");
    CHECK(o.stats.interval >= 1 && o.stats.interval <= 60);
    CHECK(o.stats.migrants_sent > 0);
    check_message_balance(o);
    check_global_agreement(p, o);
}

int main(int argc, char** argv) {
    MpiSession mpi(argc, argv);
    g_rank = mpi.rank();
    if (mpi.size() < 2) {
        std::fprintf(stderr, "qmap_mpi_tests needs at least 2 ranks (mpiexec -n 3 ...)\n");
        return 2;
    }
    const struct {
        const char* name;
        void (*fn)(const CpuProblem&);
    } cases[] = {{"fixed5_all", test_fixed_all},
                 {"remote_stop_on_target", test_remote_stop},
                 {"ring_adaptiveB", [](const CpuProblem& p) { test_ring_adaptive(p, Scheme::B); }},
                 {"ring_adaptiveA", [](const CpuProblem& p) { test_ring_adaptive(p, Scheme::A); }}};
    int exit_code = 0;
    try {
        const CpuProblem p = load_problem("qft_n8");
        for (const auto& c : cases) {
            const int before = g_failures;
            c.fn(p);
            if (mpi.rank() == 0) std::printf("%s: %s\n", c.name, g_failures == before ? "ok" : "FAILED (rank 0)");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "rank %d: exception: %s\n", g_rank, e.what());
        MpiSession::abort(1);
    }
    exit_code = max_over_ranks(g_failures == 0 ? 0 : 1);
    return exit_code;
}
