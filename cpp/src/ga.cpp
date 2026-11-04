#include "qmap/ga.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>

namespace qmap {

uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

void random_permutation(uint8_t* out, int n, std::mt19937_64& rng) {
    std::iota(out, out + n, uint8_t{0});
    std::shuffle(out, out + n, rng);
}

int tournament_select(const std::vector<int>& fitness, int k, std::mt19937_64& rng) {
    std::uniform_int_distribution<int> pick(0, static_cast<int>(fitness.size()) - 1);
    int best = pick(rng);
    for (int i = 1; i < k; ++i) {
        const int c = pick(rng);
        if (fitness[static_cast<size_t>(c)] < fitness[static_cast<size_t>(best)]) best = c;
    }
    return best;
}

void order_crossover_cuts(const uint8_t* p1, const uint8_t* p2, uint8_t* child, int n, int a, int b) {
    bool used[256] = {};
    for (int i = a; i <= b; ++i) {
        child[i] = p1[i];
        used[p1[i]] = true;
    }
    int pos = (b + 1) % n;
    for (int i = 0; i < n; ++i) {
        const uint8_t g = p2[(b + 1 + i) % n];
        if (used[g]) continue;
        child[pos] = g;
        pos = (pos + 1) % n;
    }
}

void order_crossover(const uint8_t* p1, const uint8_t* p2, uint8_t* child, int n, std::mt19937_64& rng) {
    std::uniform_int_distribution<int> pick(0, n - 1);
    int a = pick(rng), b = pick(rng);
    if (a > b) std::swap(a, b);
    order_crossover_cuts(p1, p2, child, n, a, b);
}

void swap_mutation(uint8_t* genes, int n, std::mt19937_64& rng) {
    const int i = std::uniform_int_distribution<int>(0, n - 1)(rng);
    int j = std::uniform_int_distribution<int>(0, n - 2)(rng);
    if (j >= i) ++j;
    std::swap(genes[i], genes[j]);
}

GaResult run_ga(const GaParams& p, int n_phys, const Evaluator& evaluate, const GenerationLog& on_generation) {
    if (n_phys < 2 || n_phys > 255) throw std::invalid_argument("n_phys must be in [2, 255]");
    if (p.pop_size < 2) throw std::invalid_argument("pop_size must be >= 2");
    if (p.elite < 0 || p.elite > p.pop_size) throw std::invalid_argument("elite must be in [0, pop_size]");
    if (p.tournament_k < 1) throw std::invalid_argument("tournament_k must be >= 1");
    if (p.max_generations < 0) throw std::invalid_argument("max_generations must be >= 0");

    using clock = std::chrono::steady_clock;
    const auto seconds = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    };
    const auto t_start = clock::now();

    std::mt19937_64 rng(splitmix64(p.seed));
    const size_t n = static_cast<size_t>(n_phys);
    const size_t pop_n = static_cast<size_t>(p.pop_size);
    std::vector<uint8_t> pop(pop_n * n), next(pop_n * n);
    std::vector<int> fit(pop_n);
    std::vector<int> order(pop_n);

    GaResult res;
    res.best_fitness = -1;
    long long evals = 0;

    for (int gen = 0;; ++gen) {
        const auto t_gen = clock::now();
        if (gen == 0) {
            for (size_t i = 0; i < pop_n; ++i) random_permutation(&pop[i * n], n_phys, rng);
        } else {
            std::iota(order.begin(), order.end(), 0);
            std::partial_sort(order.begin(), order.begin() + p.elite, order.end(), [&fit](int a, int b) {
                return fit[static_cast<size_t>(a)] < fit[static_cast<size_t>(b)];
            });
            for (int e = 0; e < p.elite; ++e)
                std::copy_n(&pop[static_cast<size_t>(order[static_cast<size_t>(e)]) * n], n,
                            &next[static_cast<size_t>(e) * n]);
            std::uniform_real_distribution<double> unit(0.0, 1.0);
            for (size_t i = static_cast<size_t>(p.elite); i < pop_n; ++i) {
                const size_t a = static_cast<size_t>(tournament_select(fit, p.tournament_k, rng));
                const size_t b = static_cast<size_t>(tournament_select(fit, p.tournament_k, rng));
                uint8_t* child = &next[i * n];
                if (unit(rng) < p.p_crossover)
                    order_crossover(&pop[a * n], &pop[b * n], child, n_phys, rng);
                else
                    std::copy_n(&pop[a * n], n, child);
                if (unit(rng) < p.p_mutation) swap_mutation(child, n_phys, rng);
            }
            pop.swap(next);
        }
        const auto t_ops_end = clock::now();
        evaluate(pop.data(), p.pop_size, fit.data());
        const auto t_eval_end = clock::now();
        evals += p.pop_size;

        long long sum = 0;
        size_t best_i = 0;
        for (size_t i = 0; i < pop_n; ++i) {
            sum += fit[i];
            if (fit[i] < fit[best_i]) best_i = i;
        }
        if (res.best_fitness < 0 || fit[best_i] < res.best_fitness) {
            res.best_fitness = fit[best_i];
            res.best.assign(&pop[best_i * n], &pop[best_i * n] + n);
        }

        GenerationStats s;
        s.generation = gen;
        s.elapsed_s = seconds(t_start, t_eval_end);
        s.best = fit[best_i];
        s.mean = static_cast<double>(sum) / static_cast<double>(pop_n);
        s.gen_time_s = seconds(t_gen, t_eval_end);
        s.eval_time_s = seconds(t_ops_end, t_eval_end);
        s.ops_time_s = seconds(t_gen, t_ops_end);
        s.evals = evals;
        if (on_generation) on_generation(s);

        res.generations = gen;
        res.evals = evals;
        res.total_time_s = s.elapsed_s;
        if (p.target >= 0 && res.best_fitness <= p.target) {
            res.ttt_s = s.elapsed_s;
            res.stop_reason = "target";
            break;
        }
        if (gen >= p.max_generations) {
            res.stop_reason = "max_generations";
            break;
        }
        if (s.elapsed_s >= p.max_time_s) {
            res.stop_reason = "max_time";
            break;
        }
    }
    return res;
}

}  // namespace qmap
