#include "qmap/output.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "qmap/qasm_io.hpp"

namespace qmap {

namespace {
void write_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);  // binary: keep "\n" line endings on Windows
    if (!f) throw std::runtime_error("cannot write " + path);
    f << text;
}
}  // namespace

void write_routed(const Device& dev, const RoutedCircuit& r, const std::string& qasm_path,
                  const std::string& layout_path) {
    if (!qasm_path.empty()) write_file(qasm_path, emit_qasm(dev.n_phys, r.gates));
    if (!layout_path.empty()) {
        nlohmann::json j;
        j["initial"] = r.initial;
        j["final"] = r.final_layout;
        j["swaps"] = r.swaps;
        write_file(layout_path, j.dump() + "\n");
    }
}

}  // namespace qmap
