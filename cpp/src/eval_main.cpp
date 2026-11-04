// qmap_eval: route one instance with a given initial layout and report the SWAP count.
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "qmap/device.hpp"
#include "qmap/qasm_io.hpp"
#include "qmap/route_circuit.hpp"

namespace {

const char* kUsage =
    "usage: qmap_eval --instance <qasm> --device <json> [--layout 3,0,5,...] "
    "[--out-qasm <path>] [--out-layout <path>]\n";

std::vector<int> parse_layout(const std::string& s) {
    std::vector<int> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        size_t used = 0;
        const int x = std::stoi(tok, &used);
        if (used != tok.size()) throw std::runtime_error("bad layout entry '" + tok + "'");
        v.push_back(x);
    }
    return v;
}

void write_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);  // binary: keep "\n" line endings on Windows
    if (!f) throw std::runtime_error("cannot write " + path);
    f << text;
}

}  // namespace

int main(int argc, char** argv) {
    std::string instance, device, layout_arg, out_qasm, out_layout;
    bool have_layout = false;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
            const std::string v = argv[++i];
            if (a == "--instance") instance = v;
            else if (a == "--device") device = v;
            else if (a == "--layout") { layout_arg = v; have_layout = true; }
            else if (a == "--out-qasm") out_qasm = v;
            else if (a == "--out-layout") out_layout = v;
            else throw std::runtime_error("unknown option " + a);
        }
        if (instance.empty() || device.empty()) throw std::runtime_error("--instance and --device are required");

        const qmap::Device dev = qmap::load_device(device);
        const qmap::Circuit circuit = qmap::parse_qasm_file(instance);

        std::vector<int> layout;
        if (have_layout) {
            layout = parse_layout(layout_arg);
        } else {
            layout.resize(static_cast<size_t>(circuit.n_qubits));
            std::iota(layout.begin(), layout.end(), 0);
        }
        // route_circuit validates length, range and duplicates.
        const qmap::RoutedCircuit r = qmap::route_circuit(dev, circuit, layout);

        std::printf("swaps=%d\n", r.swaps);
        if (!out_qasm.empty()) write_file(out_qasm, qmap::emit_qasm(dev.n_phys, r.gates));
        if (!out_layout.empty()) {
            nlohmann::json j;
            j["initial"] = r.initial;
            j["final"] = r.final_layout;
            j["swaps"] = r.swaps;
            write_file(out_layout, j.dump() + "\n");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n%s", e.what(), kUsage);
        return 2;
    }
    return 0;
}
