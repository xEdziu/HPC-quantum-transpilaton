// GPU fitness vs CPU count_swaps (no framework). Exit code 0 iff every CHECK passed.
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "qmap/device.hpp"
#include "qmap/gpu_fitness.hpp"
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

static std::vector<fs::path> instance_files() {
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(data_path("instances")))
        if (e.path().extension() == ".qasm") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    return files;
}

// CPU reference: same convention as the kernel (logical i starts at gene[i], i < n_logical).
static int cpu_fitness(const Device& dev, const std::vector<uint8_t>& pairs, int n_logical, const uint8_t* genes) {
    const int n = dev.n_phys;
    std::vector<uint8_t> l2p(genes, genes + n_logical), p2l(static_cast<size_t>(n), kEmpty);
    for (int i = 0; i < n_logical; ++i) p2l[genes[i]] = static_cast<uint8_t>(i);
    return count_swaps(pairs.data(), static_cast<int>(pairs.size() / 2), l2p.data(), p2l.data(), dev.dist.data(),
                       dev.next_hop.data(), n);
}

// Row 0 is the identity, the rest are random permutations.
static std::vector<uint8_t> make_population(int n_phys, int pop_size, std::mt19937& rng) {
    std::vector<uint8_t> pop(static_cast<size_t>(pop_size) * n_phys);
    std::vector<uint8_t> perm(static_cast<size_t>(n_phys));
    std::iota(perm.begin(), perm.end(), uint8_t{0});
    for (int i = 0; i < pop_size; ++i) {
        if (i > 0) std::shuffle(perm.begin(), perm.end(), rng);
        std::copy(perm.begin(), perm.end(), pop.begin() + static_cast<size_t>(i) * n_phys);
    }
    return pop;
}

// Returns the number of mismatches between GPU and CPU for the first pop_size rows of `pop`.
static int compare(GpuFitness& gpu, const Device& dev, const std::vector<uint8_t>& pairs, int n_logical,
                   const std::vector<uint8_t>& pop, int pop_size) {
    std::vector<int> out(static_cast<size_t>(pop_size), -1);
    gpu.evaluate(pop.data(), pop_size, out.data());
    int bad = 0;
    for (int i = 0; i < pop_size; ++i)
        if (out[static_cast<size_t>(i)] != cpu_fitness(dev, pairs, n_logical, &pop[static_cast<size_t>(i) * dev.n_phys]))
            ++bad;
    return bad;
}

static void test_instances() {
    const Device dev = load_device(data_path("devices/falcon27.json"));
    std::mt19937 rng(12345);
    constexpr int kPop = 10000;
    for (const auto& file : instance_files()) {
        const Circuit c = parse_qasm_file(file.string());
        const std::vector<uint8_t> pairs = two_qubit_pairs(c);
        GpuFitness gpu(dev, pairs, c.n_qubits, kPop);
        const std::vector<uint8_t> pop = make_population(dev.n_phys, kPop, rng);
        const int bad = compare(gpu, dev, pairs, c.n_qubits, pop, kPop);
        std::printf("  %-14s pop=%d mismatches=%d\n", file.stem().string().c_str(), kPop, bad);
        CHECK(bad == 0);
    }
}

static void test_odd_sizes() {
    const Device dev = load_device(data_path("devices/falcon27.json"));
    const Circuit c = parse_qasm_file(data_path("instances/qft_n12.qasm"));
    const std::vector<uint8_t> pairs = two_qubit_pairs(c);
    std::mt19937 rng(777);
    GpuFitness gpu(dev, pairs, c.n_qubits, 1000);
    const std::vector<uint8_t> pop = make_population(dev.n_phys, 1000, rng);
    for (const int n : {1000, 257, 1}) {
        const int bad = compare(gpu, dev, pairs, c.n_qubits, pop, n);
        std::printf("  qft_n12        pop=%d mismatches=%d\n", n, bad);
        CHECK(bad == 0);
    }
}

static void test_pop_too_large_throws() {
    const Device dev = load_device(data_path("devices/falcon27.json"));
    const Circuit c = parse_qasm_file(data_path("instances/qft_n8.qasm"));
    GpuFitness gpu(dev, two_qubit_pairs(c), c.n_qubits, 16);
    std::mt19937 rng(1);
    const std::vector<uint8_t> pop = make_population(dev.n_phys, 17, rng);
    std::vector<int> out(17);
    bool threw = false;
    try {
        gpu.evaluate(pop.data(), 17, out.data());
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    const struct {
        const char* name;
        void (*fn)();
    } cases[] = {{"gpu_vs_cpu_instances", test_instances},
                 {"odd_pop_sizes", test_odd_sizes},
                 {"pop_too_large_throws", test_pop_too_large_throws}};
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
