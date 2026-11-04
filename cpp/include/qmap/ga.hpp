#pragma once
// Single-island genetic algorithm over permutations of the physical qubits (v1, deliberately
// naive: host-only single-threaded operators, whole population re-evaluated every generation).
// Host code only; the fitness evaluator is injected (GPU in qmap_ga, CPU router in tests).
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace qmap {

// Seed mixer: per-rank seeds are splitmix64(base_seed ^ rank).
uint64_t splitmix64(uint64_t x);

// Operators work on rows of n genes. Lower fitness is better.
void random_permutation(uint8_t* out, int n, std::mt19937_64& rng);

// Returns the index of the best of k uniformly sampled individuals (ties: first sampled).
int tournament_select(const std::vector<int>& fitness, int k, std::mt19937_64& rng);

// Order crossover with cut points a <= b: child[a..b] = p1[a..b]; the remaining positions
// (b+1, b+2, ... wrapping) are filled with the genes missing from the segment in the order they
// appear in p2 starting after b. The rng overload draws the cuts uniformly.
void order_crossover_cuts(const uint8_t* p1, const uint8_t* p2, uint8_t* child, int n, int a, int b);
void order_crossover(const uint8_t* p1, const uint8_t* p2, uint8_t* child, int n, std::mt19937_64& rng);

// Swaps two distinct random genes (may move a logical qubit onto a free physical qubit).
void swap_mutation(uint8_t* genes, int n, std::mt19937_64& rng);

struct GaParams {
    int pop_size = 4096;
    int max_generations = 1000;
    double max_time_s = 60.0;
    int target = -1;  // stop when best <= target; < 0 = no target
    uint64_t seed = 0;
    int tournament_k = 3;
    double p_crossover = 0.9;
    double p_mutation = 0.3;  // per child
    int elite = 2;
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
    std::string stop_reason;  // "target", "max_generations" or "max_time"
};

// pop: pop_size * n_phys genes, out: pop_size fitness values.
using Evaluator = std::function<void(const uint8_t* pop, int pop_size, int* out)>;
using GenerationLog = std::function<void(const GenerationStats&)>;

// Throws std::invalid_argument on bad parameters.
GaResult run_ga(const GaParams& params, int n_phys, const Evaluator& evaluate, const GenerationLog& on_generation);

}  // namespace qmap
