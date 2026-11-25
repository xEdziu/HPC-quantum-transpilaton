#pragma once
// Single-island genetic algorithm over permutations of the physical qubits. Whole population
// re-evaluated every generation. Host code only; the fitness evaluator is injected (GPU in qmap_ga,
// CPU router in tests). V1: the children of a generation are built in parallel (OpenMP), each from
// its own counter-seeded generator, so a run does not depend on the number of threads.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace qmap {

// Seed mixer: per-rank seeds are splitmix64(base_seed ^ rank).
uint64_t splitmix64(uint64_t x);

// Small, cheap generator (one per child): SplitMix64 stream. Satisfies UniformRandomBitGenerator.
struct SplitMix64 {
    using result_type = uint64_t;
    uint64_t state;
    explicit SplitMix64(uint64_t seed) : state(seed) {}
    // (min)/(max) in parentheses: windows.h (via mpi.h) defines min/max macros
    static constexpr result_type(min)() { return 0; }
    static constexpr result_type(max)() { return ~result_type{0}; }
    result_type operator()() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

// Operators work on rows of n genes. Lower fitness is better. Rng: any UniformRandomBitGenerator.
template <class Rng>
void random_permutation(uint8_t* out, int n, Rng& rng) {
    std::iota(out, out + n, uint8_t{0});
    std::shuffle(out, out + n, rng);
}

// Returns the index of the best of k uniformly sampled individuals (ties: first sampled).
template <class Rng>
int tournament_select(const int* fitness, int pop_size, int k, Rng& rng) {
    std::uniform_int_distribution<int> pick(0, pop_size - 1);
    int best = pick(rng);
    for (int i = 1; i < k; ++i) {
        const int c = pick(rng);
        if (fitness[c] < fitness[best]) best = c;
    }
    return best;
}
template <class Rng>
int tournament_select(const std::vector<int>& fitness, int k, Rng& rng) {
    return tournament_select(fitness.data(), static_cast<int>(fitness.size()), k, rng);
}

// Order crossover with cut points a <= b: child[a..b] = p1[a..b]; the remaining positions
// (b+1, b+2, ... wrapping) are filled with the genes missing from the segment in the order they
// appear in p2 starting after b. The rng overload draws the cuts uniformly.
void order_crossover_cuts(const uint8_t* p1, const uint8_t* p2, uint8_t* child, int n, int a, int b);
template <class Rng>
void order_crossover(const uint8_t* p1, const uint8_t* p2, uint8_t* child, int n, Rng& rng) {
    std::uniform_int_distribution<int> pick(0, n - 1);
    int a = pick(rng), b = pick(rng);
    if (a > b) std::swap(a, b);
    order_crossover_cuts(p1, p2, child, n, a, b);
}

// Swaps two distinct random genes (may move a logical qubit onto a free physical qubit).
template <class Rng>
void swap_mutation(uint8_t* genes, int n, Rng& rng) {
    const int i = std::uniform_int_distribution<int>(0, n - 1)(rng);
    int j = std::uniform_int_distribution<int>(0, n - 2)(rng);
    if (j >= i) ++j;
    std::swap(genes[i], genes[j]);
}

// Seed of the generator that builds individual i of generation gen (0 = initial population).
uint64_t child_seed(uint64_t run_seed, int gen, size_t i);

// OpenMP threads used by run_ga for threads <= 0 (all available) or the given count.
int effective_threads(int threads);

struct GaParams {
    int pop_size = 4096;
    int max_generations = 1000;
    double max_time_s = 60.0;
    int target = -1;  // stop when best <= target; < 0 = no target
    // Optional extra thresholds timed in the same run (GaResult::ttt_by_target); does not affect stopping.
    // The caller sets target = min(targets) to stop on the strictest one.
    std::vector<int> targets;
    uint64_t seed = 0;
    int tournament_k = 3;
    double p_crossover = 0.9;
    double p_mutation = 0.3;  // per child
    int elite = 2;
    int threads = 0;  // host threads for the GA operators; <= 0 = all available (OpenMP default)
};

struct GenerationStats {
    int generation = 0;  // 0 = initial population
    double elapsed_s = 0;
    int best = 0;
    double mean = 0;
    double gen_time_s = 0;
    double eval_time_s = 0;  // wall time of the evaluate call
    double ops_time_s = 0;   // selection + crossover + mutation + elitism (init for generation 0)
    long long evals = 0;     // individuals evaluated so far
};

struct GaResult {
    std::vector<uint8_t> best;  // n_phys genes
    int best_fitness = 0;
    int generations = 0;  // last generation index run
    long long evals = 0;
    double total_time_s = 0;
    double ttt_s = -1;        // elapsed time when best <= target first happened, < 0 if never
    std::vector<double> ttt_by_target;  // per GaParams::targets entry: same clock as ttt_s, < 0 if never
    std::string stop_reason;  // "target", "max_generations", "max_time" or "external" (hook asked to stop)
};

// What an island hook may see and do once per generation (after evaluation, stats and the
// on_generation callback, before the stop checks). Constructed by run_ga only.
class IslandView {
public:
    IslandView(int generation, int n_phys, uint8_t* pop, int* fit, size_t pop_n, size_t best_index, GaResult* res)
        : generation_(generation), n_(n_phys), pop_(pop), fit_(fit), pop_n_(pop_n), best_index_(best_index), res_(res) {}

    int generation() const { return generation_; }
    // Best fitness of the population as evaluated this generation and a pointer to its n_phys genes.
    int best_fitness() const { return fit_[best_index_]; }
    const uint8_t* best_genes() const { return pop_ + best_index_ * static_cast<size_t>(n_); }

    // Overwrites the current worst individual (genes and fitness) in place. If it beats the run's
    // best-so-far, that is updated too. Several immigrants in one call may overwrite each other.
    void immigrate(const uint8_t* genes, int fitness);

private:
    int generation_, n_;
    uint8_t* pop_;
    int* fit_;
    size_t pop_n_, best_index_;
    GaResult* res_;
};

// Returns false to stop the run (stop_reason "external", unless the target was reached).
using IslandHook = std::function<bool(IslandView&)>;

// pop: pop_size * n_phys genes, out: pop_size fitness values.
using Evaluator = std::function<void(const uint8_t* pop, int pop_size, int* out)>;
using GenerationLog = std::function<void(const GenerationStats&)>;

// Throws std::invalid_argument on bad parameters.
GaResult run_ga(const GaParams& params, int n_phys, const Evaluator& evaluate, const GenerationLog& on_generation,
                const IslandHook& hook = {});

}  // namespace qmap
