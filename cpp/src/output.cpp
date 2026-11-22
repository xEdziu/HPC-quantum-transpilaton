#include "qmap/output.hpp"

#include <filesystem>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "qmap/qasm_io.hpp"

namespace qmap {

std::ofstream open_output(const std::string& path) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    std::error_code ec;
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);  // failure surfaces on open below
    std::ofstream f(path, std::ios::binary);  // binary: keep "\n" line endings on Windows
    if (!f) throw std::runtime_error("cannot write " + path);
    return f;
}

namespace {
void write_file(const std::string& path, const std::string& text) {
    std::ofstream f = open_output(path);
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
