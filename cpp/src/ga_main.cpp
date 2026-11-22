// qmap_ga: island GA over initial layouts, fitness evaluated on the GPU, islands connected by MPI
// (one rank = one island; also runs as a single island without mpiexec).
#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <nvtx3/nvToolsExt.h>

#include "qmap/device.hpp"
#include "qmap/ga.hpp"
#include "qmap/gpu_fitness.hpp"
#include "qmap/island_mpi.hpp"
#include "qmap/migration.hpp"
#include "qmap/output.hpp"
#include "qmap/qasm_io.hpp"
#include "qmap/route_circuit.hpp"

namespace {

const char* kUsage =
    "usage: qmap_ga --instance <qasm> --device <json> [--pop N] [--generations N] [--max-time S] [--target N[,N...]]\n"
    "               [--seed N] [--tournament K] [--pc P] [--pm P] [--elite E] [--log <csv>]\n"
    "               [--migration fixed:N|adaptiveA|adaptiveB] [--topology all|ring] [--out-qasm <path> --out-layout <path>]\n"
    "               [--summary <json>]\n"
    "  --log writes one CSV per rank: results/run.csv -> results/run_r0.csv, results/run_r1.csv, ...\n"
    "  --summary: rank 0 writes the global result and run parameters as JSON\n"
    "  --target: one value, or a comma-separated list (e.g. 172,164,161): the run stops at the smallest and\n"
    "            records the time to each; -1 = no target. Every rank must get the same list.\n";

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

// "172,164,161" -> {172, 164, 161}; a single negative value means "no target" (empty list).
std::vector<int> to_targets(const std::string& a, const std::string& v) {
    std::vector<int> out;
    size_t pos = 0;
    for (;;) {
        const size_t comma = v.find(',', pos);
        const int t = to_int(a, v.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
        if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (out.size() == 1 && out[0] < 0) out.clear();
    for (const int t : out)
        if (t < 0) throw std::invalid_argument("negative target in list for " + a + ": '" + v + "'");
    return out;
}

// "172:1.230000,164:NA"
std::string format_ttt_by_target(const std::vector<int>& targets, const std::vector<double>& ttt) {
    std::string s;
    for (size_t i = 0; i < targets.size(); ++i) {
        char t[32] = "NA";
        if (ttt[i] >= 0) std::snprintf(t, sizeof t, "%.6f", ttt[i]);
        s += (i ? "," : "") + std::to_string(targets[i]) + ":" + t;
    }
    return s;
}

// "dir/run.csv" -> "dir/run_r<rank>.csv" (suffix appended if there is no extension). No "{rank}"-style
// placeholder on purpose: braces in unquoted arguments get mangled on the PowerShell/mpiexec command line.
std::string with_rank(const std::string& path, int rank) {
    const std::string suffix = "_r" + std::to_string(rank);
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path + suffix;
    return path.substr(0, dot) + suffix + path.substr(dot);
}

}  // namespace

int main(int argc, char** argv) {
    qmap::MpiSession mpi(argc, argv);
    std::string instance, device, log_path, out_qasm, out_layout, summary_path, migration_arg = "fixed:100",
        topology_arg = "all";
    qmap::GaParams params;
    uint64_t seed_arg = params.seed;  // user seed; params.seed is replaced by the per-rank seed
    qmap::MigrationPolicy migration;
    qmap::Topology topology = qmap::Topology::All;
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
            else if (a == "--target") {
                params.targets = to_targets(a, v);
                params.target =
                    params.targets.empty() ? -1 : *std::min_element(params.targets.begin(), params.targets.end());
            }
            else if (a == "--seed") { params.seed = to_u64(a, v); seed_arg = params.seed; }
            else if (a == "--tournament") params.tournament_k = to_int(a, v);
            else if (a == "--pc") params.p_crossover = to_double(a, v);
            else if (a == "--pm") params.p_mutation = to_double(a, v);
            else if (a == "--elite") params.elite = to_int(a, v);
            else if (a == "--log") log_path = v;
            else if (a == "--migration") { migration = qmap::parse_migration(v); migration_arg = v; }
            else if (a == "--topology") { topology = qmap::parse_topology(v); topology_arg = v; }
            else if (a == "--out-qasm") out_qasm = v;
            else if (a == "--out-layout") out_layout = v;
            else if (a == "--summary") summary_path = v;
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
        params.seed = qmap::rank_seed(params.seed, mpi.rank());

        qmap::Island island(
            dev.n_phys, topology, migration, params.max_generations, [](const char* name) { nvtxRangePushA(name); },
            [] { nvtxRangePop(); });

        const qmap::Evaluator evaluate = [&gpu](const uint8_t* pop, int pop_size, int* out) {
            nvtxRangePushA("ga_evaluate");
            gpu.evaluate(pop, pop_size, out);
            nvtxRangePop();
        };

        std::ofstream csv;
        if (!log_path.empty()) {
            const std::string path = with_rank(log_path, mpi.rank());
            csv = qmap::open_output(path);
            csv <<"generation,elapsed_s,best,mean,gen_time_s,eval_time_s,ops_time_s,evals,evals_per_s,"
                   "migrants_sent,migrants_received,interval\n";
        }
        const qmap::GenerationLog log = [&csv, &island](const qmap::GenerationStats& s) {
            if (!csv.is_open()) return;
            const double rate = s.elapsed_s > 0 ? static_cast<double>(s.evals) / s.elapsed_s : 0.0;
            const qmap::IslandStats& m = island.stats();
            char row[320];
            std::snprintf(row, sizeof row, "%d,%.6f,%d,%.4f,%.6f,%.6f,%.6f,%lld,%.1f,%lld,%lld,%d\n", s.generation,
                          s.elapsed_s, s.best, s.mean, s.gen_time_s, s.eval_time_s, s.ops_time_s, s.evals, rate,
                          m.migrants_sent, m.migrants_received, m.interval);
            csv << row;
        };

        island.synchronize_start();
        nvtxRangePushA("ga_run");
        qmap::GaResult r = qmap::run_ga(params, dev.n_phys, evaluate, log, island.hook());
        nvtxRangePop();
        if (csv.is_open()) csv.flush();
        const qmap::GlobalResult global = island.finish(r);  // collective; also maps "external" to "remote_stop"

        char ttt[32] = "NA";
        if (r.ttt_s >= 0) std::snprintf(ttt, sizeof ttt, "%.6f", r.ttt_s);
        std::printf("rank=%d best=%d generations=%d evals=%lld time_s=%.6f ttt_s=%s stop=%s migrants_sent=%lld "
                    "migrants_received=%lld\n",
                    mpi.rank(), r.best_fitness, r.generations, r.evals, r.total_time_s, ttt, r.stop_reason.c_str(),
                    island.stats().migrants_sent, island.stats().migrants_received);

        if (mpi.rank() == 0) {
            // Sanity: the GPU fitness of the best chromosome must equal the host router's SWAP count.
            std::vector<int> layout(global.best.begin(), global.best.begin() + circuit.n_qubits);
            const qmap::RoutedCircuit routed = qmap::route_circuit(dev, circuit, layout);
            if (routed.swaps != global.best_fitness)
                throw std::runtime_error("GPU fitness " + std::to_string(global.best_fitness) +
                                         " != host router swaps " + std::to_string(routed.swaps));
            qmap::write_routed(dev, routed, out_qasm, out_layout);
            char gttt[32] = "NA";
            if (global.ttt_s >= 0) std::snprintf(gttt, sizeof gttt, "%.6f", global.ttt_s);
            std::string by_target;
            if (!params.targets.empty())
                by_target = " ttt_by_target=" + format_ttt_by_target(params.targets, global.ttt_by_target);
            std::printf("global best=%d best_rank=%d ttt_s=%s evals=%lld time_s=%.6f%s\n", global.best_fitness,
                        global.best_rank, gttt, global.evals, global.time_s, by_target.c_str());

            if (!summary_path.empty()) {
                nlohmann::json j;
                j["best"] = global.best_fitness;
                j["best_rank"] = global.best_rank;
                j["ttt_s"] = global.ttt_s >= 0 ? nlohmann::json(global.ttt_s) : nlohmann::json(nullptr);
                j["evals"] = global.evals;
                j["time_s"] = global.time_s;
                j["rank0_stop"] = r.stop_reason;  // other ranks' reasons are only on stdout
                j["instance"] = instance;
                j["pop"] = params.pop_size;  // rank 0 only
                j["seed"] = seed_arg;
                j["target"] = params.target;  // the stopping (smallest) target
                j["targets"] = params.targets;
                nlohmann::json ttt_map = nlohmann::json::object();
                for (size_t i = 0; i < params.targets.size(); ++i)
                    ttt_map[std::to_string(params.targets[i])] = global.ttt_by_target[i] >= 0
                                                                       ? nlohmann::json(global.ttt_by_target[i])
                                                                       : nlohmann::json(nullptr);
                j["ttt_by_target"] = ttt_map;
                j["migration"] = migration_arg;
                j["topology"] = topology_arg;
                j["n_ranks"] = mpi.size();
                std::ofstream out = qmap::open_output(summary_path);
                out <<j.dump(2) << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "rank=%d error: %s\n", mpi.rank(), e.what());
        qmap::MpiSession::abort(1);
    }
    return 0;
}
