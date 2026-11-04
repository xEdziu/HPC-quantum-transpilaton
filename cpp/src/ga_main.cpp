// qmap_ga: single-island GA over initial layouts, fitness evaluated on the GPU.
#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nvtx3/nvToolsExt.h>

#include "qmap/device.hpp"
#include "qmap/ga.hpp"
#include "qmap/gpu_fitness.hpp"
#include "qmap/output.hpp"
#include "qmap/qasm_io.hpp"
#include "qmap/route_circuit.hpp"

namespace {

const char* kUsage =
    "usage: qmap_ga --instance <qasm> --device <json> [--pop N] [--generations N] [--max-time S] [--target N]\n"
    "               [--seed N] [--tournament K] [--pc P] [--pm P] [--elite E] [--log <csv>]\n"
    "               [--out-qasm <path> --out-layout <path>]\n";

int to_int(const std::string& a, const std::string& v) {
    size_t used = 0;
    const int x = std::stoi(v, &used);
    if (used != v.size()) throw std::invalid_argument("bad integer for " + a + ": '" + v + "'");
    return x;
}

double to_double(const std::string& a, const std::string& v) {
    size_t used = 0;
    const double x = std::stod(v, &used);
    if (used != v.size()) throw std::invalid_argument("bad number for " + a + ": '" + v + "'");
    return x;
}

uint64_t to_u64(const std::string& a, const std::string& v) {
    size_t used = 0;
    if (!v.empty() && v[0] == '-') throw std::invalid_argument("bad seed for " + a + ": '" + v + "'");
    const uint64_t x = std::stoull(v, &used);
    if (used != v.size()) throw std::invalid_argument("bad seed for " + a + ": '" + v + "'");
    return x;
}

}  // namespace

int main(int argc, char** argv) {
    std::string instance, device, log_path, out_qasm, out_layout;
    qmap::GaParams params;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
            const std::string v = argv[++i];
            if (a == "--instance") instance = v;
            else if (a == "--device") device = v;
            else if (a == "--pop") params.pop_size = to_int(a, v);
            else if (a == "--generations") params.max_generations = to_int(a, v);
            else if (a == "--max-time") params.max_time_s = to_double(a, v);
            else if (a == "--target") params.target = to_int(a, v);
            else if (a == "--seed") params.seed = to_u64(a, v);
            else if (a == "--tournament") params.tournament_k = to_int(a, v);
            else if (a == "--pc") params.p_crossover = to_double(a, v);
            else if (a == "--pm") params.p_mutation = to_double(a, v);
            else if (a == "--elite") params.elite = to_int(a, v);
            else if (a == "--log") log_path = v;
            else if (a == "--out-qasm") out_qasm = v;
            else if (a == "--out-layout") out_layout = v;
            else throw std::invalid_argument("unknown option " + a);
        }
        if (instance.empty() || device.empty()) throw std::invalid_argument("--instance and --device are required");
        if (params.pop_size < 2) throw std::invalid_argument("--pop must be >= 2");
        if (params.max_generations < 0) throw std::invalid_argument("--generations must be >= 0");
        if (params.max_time_s <= 0) throw std::invalid_argument("--max-time must be > 0");
        if (params.tournament_k < 1) throw std::invalid_argument("--tournament must be >= 1");
        if (params.p_crossover < 0 || params.p_crossover > 1) throw std::invalid_argument("--pc must be in [0, 1]");
        if (params.p_mutation < 0 || params.p_mutation > 1) throw std::invalid_argument("--pm must be in [0, 1]");
        if (params.elite < 0 || params.elite > params.pop_size)
            throw std::invalid_argument("--elite must be in [0, pop]");
        if (out_qasm.empty() != out_layout.empty())
            throw std::invalid_argument("--out-qasm and --out-layout must be given together");
    } catch (const std::exception& e) {  // includes std::stoi/stod errors
        std::fprintf(stderr, "error: %s\n%s", e.what(), kUsage);
        return 2;
    }

    try {
        const qmap::Device dev = qmap::load_device(device);
        const qmap::Circuit circuit = qmap::parse_qasm_file(instance);
        if (circuit.n_qubits > dev.n_phys) throw std::runtime_error("circuit has more qubits than the device");
        qmap::GpuFitness gpu(dev, qmap::two_qubit_pairs(circuit), circuit.n_qubits, params.pop_size);

        const qmap::Evaluator evaluate = [&gpu](const uint8_t* pop, int pop_size, int* out) {
            nvtxRangePushA("ga_evaluate");
            gpu.evaluate(pop, pop_size, out);
            nvtxRangePop();
        };

        std::ofstream csv;
        if (!log_path.empty()) {
            csv.open(log_path, std::ios::binary);  // binary: "\n" endings on Windows
            if (!csv) throw std::runtime_error("cannot write " + log_path);
            csv << "generation,elapsed_s,best,mean,gen_time_s,eval_time_s,ops_time_s,evals,evals_per_s\n";
        }
        const qmap::GenerationLog log = [&csv](const qmap::GenerationStats& s) {
            if (!csv.is_open()) return;
            const double rate = s.elapsed_s > 0 ? static_cast<double>(s.evals) / s.elapsed_s : 0.0;
            char row[256];
            std::snprintf(row, sizeof row, "%d,%.6f,%d,%.4f,%.6f,%.6f,%.6f,%lld,%.1f\n", s.generation, s.elapsed_s,
                          s.best, s.mean, s.gen_time_s, s.eval_time_s, s.ops_time_s, s.evals, rate);
            csv << row;
        };

        nvtxRangePushA("ga_run");
        const qmap::GaResult r = qmap::run_ga(params, dev.n_phys, evaluate, log);
        nvtxRangePop();
        if (csv.is_open()) csv.flush();

        // Sanity: the GPU fitness of the best chromosome must equal the host router's SWAP count.
        std::vector<int> layout(r.best.begin(), r.best.begin() + circuit.n_qubits);
        const qmap::RoutedCircuit routed = qmap::route_circuit(dev, circuit, layout);
        if (routed.swaps != r.best_fitness)
            throw std::runtime_error("GPU fitness " + std::to_string(r.best_fitness) +
                                     " != host router swaps " + std::to_string(routed.swaps));
        qmap::write_routed(dev, routed, out_qasm, out_layout);

        char ttt[32] = "NA";
        if (r.ttt_s >= 0) std::snprintf(ttt, sizeof ttt, "%.6f", r.ttt_s);
        std::printf("best=%d generations=%d evals=%lld time_s=%.6f ttt_s=%s stop=%s\n", r.best_fitness, r.generations,
                    r.evals, r.total_time_s, ttt, r.stop_reason.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
