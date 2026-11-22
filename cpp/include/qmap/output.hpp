#pragma once
// Writing of routed results: routed QASM plus a layout JSON {"initial","final","swaps"}.
#include <fstream>
#include <string>

#include "qmap/device.hpp"
#include "qmap/route_circuit.hpp"

namespace qmap {

// Opens `path` for binary writing ("\n" endings on Windows), creating missing parent directories first
// (each MPI rank writes on its own host, where the run directory may not exist yet).
// Throws std::runtime_error if the file cannot be opened.
std::ofstream open_output(const std::string& path);

// Empty path = skip that file. Files are written in binary mode ("\n" endings on Windows).
// Throws std::runtime_error if a file cannot be written.
void write_routed(const Device& dev, const RoutedCircuit& r, const std::string& qasm_path,
                  const std::string& layout_path);

}  // namespace qmap
