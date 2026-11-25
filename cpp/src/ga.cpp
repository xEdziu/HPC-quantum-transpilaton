#include "qmap/ga.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace qmap {

uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
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

uint64_t child_seed(uint64_t run_seed, int gen, size_t i) {
    // two mixing rounds: (gen, i) pairs of neighbouring children get unrelated streams
    return splitmix64(splitmix64(run_seed ^ (static_cast<uint64_t>(gen) << 40)) ^ static_cast<uint64_t>(i));
}

int effective_threads(int threads) {
#ifdef _OPENMP
    return threads > 0 ? threads : omp_get_max_threads();
#else
    (void)threads;
    return 1;
#endif
}

void IslandView::immigrate(const uint8_t* genes, int fitness) {
    size_t worst = 0;
    for (size_t i = 1; i < pop_n_; ++i)
        if (fit_[i] > fit_[worst]) worst = i;
    std::copy_n(genes, n_, pop_ + worst * static_cast<size_t>(n_));
    fit_[worst] = fitness;
    if (fitness < res_->best_fitness) {
        res_->best_fitness = fitness;
        res_->best.assign(genes, genes + n_);
    }
}

GaResult run_ga(const GaParams& p, int n_phys, const Evaluator& evaluate, const GenerationLog& on_generation,
                const IslandHook& hook) {
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

    const uint64_t run_seed = splitmix64(p.seed);
    const int threads = effective_threads(p.threads);
    const size_t n = static_cast<size_t>(n_phys);
    const size_t pop_n = static_cast<size_t>(p.pop_size);
    const int pop_i = p.pop_size;
    std::vector<uint8_t> pop(pop_n * n), next(pop_n * n);
    std::vector<int> fit(pop_n);
    std::vector<int> order(pop_n);

    GaResult res;
    res.best_fitness = -1;
    res.ttt_by_target.assign(p.targets.size(), -1.0);
    long long evals = 0;

    for (int gen = 0;; ++gen) {
        const auto t_gen = clock::now();
        if (gen == 0) {
#pragma omp parallel for schedule(static) num_threads(threads)
            for (int i = 0; i < pop_i; ++i) {
                SplitMix64 rng(child_seed(run_seed, 0, static_cast<size_t>(i)));
                random_permutation(&pop[static_cast<size_t>(i) * n], n_phys, rng);
            }
        } else {
            std::iota(order.begin(), order.end(), 0);
            std::partial_sort(order.begin(), order.begin() + p.elite, order.end(), [&fit](int a, int b) {
                return fit[static_cast<size_t>(a)] < fit[static_cast<size_t>(b)];
            });
            for (int e = 0; e < p.elite; ++e)
                std::copy_n(&pop[static_cast<size_t>(order[static_cast<size_t>(e)]) * n], n,
                            &next[static_cast<size_t>(e) * n]);
            const int* fit_p = fit.data();
            const uint8_t* pop_p = pop.data();
            uint8_t* next_p = next.data();
#pragma omp parallel for schedule(static) num_threads(threads)
            for (int i = p.elite; i < pop_i; ++i) {
                SplitMix64 rng(child_seed(run_seed, gen, static_cast<size_t>(i)));
                std::uniform_real_distribution<double> unit(0.0, 1.0);
                const size_t a = static_cast<size_t>(tournament_select(fit_p, pop_i, p.tournament_k, rng));
                const size_t b = static_cast<size_t>(tournament_select(fit_p, pop_i, p.tournament_k, rng));
                uint8_t* child = next_p + static_cast<size_t>(i) * n;
                if (unit(rng) < p.p_crossover)
                    order_crossover(pop_p + a * n, pop_p + b * n, child, n_phys, rng);
                else
                    std::copy_n(pop_p + a * n, n, child);
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
        bool keep_going = true;
        if (hook) {
            IslandView view(gen, n_phys, pop.data(), fit.data(), pop_n, best_i, &res);
            keep_going = hook(view);
        }
        for (size_t i = 0; i < p.targets.size(); ++i)  // best-so-far, immigrants included (as for ttt_s)
            if (res.ttt_by_target[i] < 0 && res.best_fitness <= p.targets[i]) res.ttt_by_target[i] = s.elapsed_s;
        if (p.target >= 0 && res.best_fitness <= p.target) {
            res.ttt_s = s.elapsed_s;
            res.stop_reason = "target";
            break;
        }
        if (!keep_going) {
            res.stop_reason = "external";
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
