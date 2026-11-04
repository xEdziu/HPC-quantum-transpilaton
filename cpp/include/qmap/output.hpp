#pragma once
// Writing of routed results: routed QASM plus a layout JSON {"initial","final","swaps"}.
#include <string>

#include "qmap/device.hpp"
#include "qmap/route_circuit.hpp"

namespace qmap {

// Empty path = skip that file. Files are written in binary mode ("\n" endings on Windows).
// Throws std::runtime_error if a file cannot be written.
void write_routed(const Device& dev, const RoutedCircuit& r, const std::string& qasm_path,
                  const std::string& layout_path);

}  // namespace qmap
