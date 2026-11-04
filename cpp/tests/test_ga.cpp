// GA operator and loop tests, CPU only (no framework). Exit code 0 iff every CHECK passed.
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "qmap/device.hpp"
#include "qmap/ga.hpp"
#include "qmap/qasm_io.hpp"
#include "qmap/router.hpp"

#ifndef QMAP_DATA_DIR
#error "QMAP_DATA_DIR must point at the repository data directory"
#endif

namespace fs = std::filesystem;
using namespace qmap;

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

static std::string data_path(const std::string& rel) { return (fs::path(QMAP_DATA_DIR) / rel).string(); }

static bool is_permutation(const std::vector<uint8_t>& g, int n) {
    std::vector<uint8_t> s(g);
    std::sort(s.begin(), s.end());
    for (int i = 0; i < n; ++i)
        if (s[static_cast<size_t>(i)] != i) return false;
    return static_cast<int>(s.size()) == n;
}

static void test_crossover() {
    constexpr int n = 27;
    std::mt19937_64 rng(1);
    std::vector<uint8_t> p1(n), p2(n), child(n);
    int bad_perm = 0, bad_segment = 0;
    for (int t = 0; t < 10000; ++t) {
        random_permutation(p1.data(), n, rng);
        random_permutation(p2.data(), n, rng);
        const int a = static_cast<int>(rng() % n), b = static_cast<int>(rng() % n);
        const int lo = std::min(a, b), hi = std::max(a, b);
        order_crossover_cuts(p1.data(), p2.data(), child.data(), n, lo, hi);
        if (!is_permutation(child, n)) ++bad_perm;
        for (int i = lo; i <= hi; ++i)
            if (child[static_cast<size_t>(i)] != p1[static_cast<size_t>(i)]) ++bad_segment;
        // The rng overload must also yield a permutation.
        order_crossover(p1.data(), p2.data(), child.data(), n, rng);
        if (!is_permutation(child, n)) ++bad_perm;
    }
    CHECK(bad_perm == 0);
    CHECK(bad_segment == 0);
}

static void test_mutation() {
    constexpr int n = 27;
    std::mt19937_64 rng(2);
    std::vector<uint8_t> g(n), before(n);
    int bad = 0, unchanged = 0;
    for (int t = 0; t < 10000; ++t) {
        random_permutation(g.data(), n, rng);
        before = g;
        swap_mutation(g.data(), n, rng);
        if (!is_permutation(g, n)) ++bad;
        int diff = 0;
        for (size_t i = 0; i < g.size(); ++i) diff += g[i] != before[i];
        if (diff != 2) ++unchanged;
    }
    CHECK(bad == 0);
    CHECK(unchanged == 0);
}

static void test_tournament() {
    std::mt19937_64 rng(3);
    std::vector<int> fit(50);
    for (int& f : fit) f = static_cast<int>(rng() % 1000);
    const int best = static_cast<int>(std::min_element(fit.begin(), fit.end()) - fit.begin());
    int out_of_range = 0, not_min = 0;
    for (int t = 0; t < 1000; ++t) {
        const int i = tournament_select(fit, 3, rng);
        if (i < 0 || i >= static_cast<int>(fit.size())) ++out_of_range;
    }
    for (int t = 0; t < 1000; ++t) {
        // k == pop_size samples with replacement, so the min is not guaranteed; use a large k instead.
        const int i = tournament_select(fit, 2000, rng);
        if (fit[static_cast<size_t>(i)] != fit[static_cast<size_t>(best)]) ++not_min;
    }
    CHECK(out_of_range == 0);
    CHECK(not_min == 0);
}

struct CpuProblem {
    Device dev;
    std::vector<uint8_t> pairs;
    int n_logical;
};

static CpuProblem load_problem(const std::string& instance) {
    CpuProblem p;
    p.dev = load_device(data_path("devices/falcon27.json"));
    const Circuit c = parse_qasm_file(data_path("instances/" + instance + ".qasm"));
    p.pairs = two_qubit_pairs(c);
    p.n_logical = c.n_qubits;
    return p;
}

static Evaluator cpu_evaluator(const CpuProblem& p) {
    return [&p](const uint8_t* pop, int pop_size, int* out) {
        const int n = p.dev.n_phys;
        for (int k = 0; k < pop_size; ++k) {
            const uint8_t* genes = pop + static_cast<size_t>(k) * n;
            std::vector<uint8_t> l2p(genes, genes + p.n_logical), p2l(static_cast<size_t>(n), kEmpty);
            for (int i = 0; i < p.n_logical; ++i) p2l[genes[i]] = static_cast<uint8_t>(i);
            out[k] = count_swaps(p.pairs.data(), static_cast<int>(p.pairs.size() / 2), l2p.data(), p2l.data(),
                                 p.dev.dist.data(), p.dev.next_hop.data(), n);
        }
    };
}

static void test_run_ga(const std::string& instance) {
    const CpuProblem p = load_problem(instance);
    const Evaluator eval = cpu_evaluator(p);
    GaParams params;
    params.pop_size = 64;
    params.max_generations = 15;
    params.max_time_s = 600;
    params.seed = 42;

    std::vector<int> seq1, seq2;
    const GaResult r1 = run_ga(params, p.dev.n_phys, eval, [&seq1](const GenerationStats& s) { seq1.push_back(s.best); });
    const GaResult r2 = run_ga(params, p.dev.n_phys, eval, [&seq2](const GenerationStats& s) { seq2.push_back(s.best); });
    std::printf("  %-10s best=%d generations=%d stop=%s\n", instance.c_str(), r1.best_fitness, r1.generations,
                r1.stop_reason.c_str());

    CHECK(r1.stop_reason == "max_generations");
    CHECK(r1.generations == params.max_generations);
    CHECK(static_cast<int>(seq1.size()) == params.max_generations + 1);
    CHECK(r1.evals == static_cast<long long>(params.max_generations + 1) * params.pop_size);
    CHECK(r1.best == r2.best);
    CHECK(r1.best_fitness == r2.best_fitness);
    CHECK(seq1 == seq2);
    CHECK(is_permutation(r1.best, p.dev.n_phys));
    CHECK(r1.ttt_s < 0);
    CHECK(r1.best_fitness == seq1.back());
    CHECK(std::is_sorted(seq1.rbegin(), seq1.rend()));  // non-increasing (elitism)
}

static void test_target_stop() {
    const CpuProblem p = load_problem("qft_n8");
    GaParams params;
    params.pop_size = 32;
    params.max_generations = 100;
    params.max_time_s = 600;
    params.target = 1000000;
    const GaResult r = run_ga(params, p.dev.n_phys, cpu_evaluator(p), {});
    CHECK(r.stop_reason == "target");
    CHECK(r.generations == 0);
    CHECK(r.ttt_s >= 0);
    CHECK(r.evals == params.pop_size);
}

static void test_island_hook() {
    const CpuProblem p = load_problem("qft_n8");
    const int n = p.dev.n_phys;
    GaParams params;
    params.pop_size = 32;
    params.max_generations = 50;
    params.max_time_s = 600;
    params.seed = 7;

    std::vector<uint8_t> migrant(static_cast<size_t>(n));
    std::iota(migrant.begin(), migrant.end(), uint8_t{0});

    // The immigrant (fake fitness 0) becomes best-so-far, is the elite of the next generation, and
    // the target check then runs on best-so-far.
    {
        const Evaluator base = cpu_evaluator(p);
        std::vector<std::vector<uint8_t>> first_rows;
        const Evaluator spy = [&](const uint8_t* pop, int pop_size, int* out) {
            first_rows.emplace_back(pop, pop + n);
            base(pop, pop_size, out);
        };
        params.target = -1;
        const GaResult r = run_ga(params, n, spy, {}, [&](IslandView& v) {
            CHECK(v.best_fitness() >= 0);
            if (v.generation() == 2) v.immigrate(migrant.data(), 0);
            return v.generation() < 4;
        });
        CHECK(r.stop_reason == "external");
        CHECK(r.generations == 4);
        CHECK(r.best_fitness == 0);
        CHECK(r.best == migrant);
        CHECK(first_rows.size() == 5);
        CHECK(first_rows[3] == migrant);  // generation 3: elite 0 is the immigrant
    }
    {
        params.target = 0;
        const GaResult r = run_ga(params, n, cpu_evaluator(p), {}, [&](IslandView& v) {
            if (v.generation() == 2) v.immigrate(migrant.data(), 0);
            return true;
        });
        CHECK(r.stop_reason == "target");
        CHECK(r.generations == 2);
        CHECK(r.ttt_s >= 0);
    }
    {
        params.target = -1;
        int best_before = -1;
        const GaResult r = run_ga(params, n, cpu_evaluator(p), {}, [&](IslandView& v) {
            if (v.generation() == 3) {
                best_before = v.best_fitness();
                v.immigrate(migrant.data(), 1000000);  // worse than everything: best-so-far unchanged
            }
            return v.generation() < 3;
        });
        CHECK(r.stop_reason == "external");
        CHECK(r.best_fitness <= best_before);
        CHECK(r.best_fitness < 1000000);
    }
}

int main() {
    const struct {
        const char* name;
        void (*fn)();
    } cases[] = {{"order_crossover", test_crossover},
                 {"swap_mutation", test_mutation},
                 {"tournament", test_tournament},
                 {"run_ga_qft_n8", [] { test_run_ga("qft_n8"); }},
                 {"run_ga_falcon27_qaoa_n12", [] { test_run_ga("qaoa_n12"); }},
                 {"target_stop", test_target_stop},
                 {"island_hook", test_island_hook}};
    for (const auto& c : cases) {
        const int before = g_failures;
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::fprintf(stderr, "%s: exception: %s\n", c.name, e.what());
        }
        std::printf("%s: %s\n", c.name, g_failures == before ? "ok" : "FAILED");
    }
    return g_failures == 0 ? 0 : 1;
}
