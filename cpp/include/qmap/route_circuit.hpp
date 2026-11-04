#pragma once
// Host-side routing of a whole circuit: runs qmap::route and rebuilds the gate stream.
#include <vector>

#include "qmap/device.hpp"
#include "qmap/qasm_io.hpp"

namespace qmap {

struct RoutedCircuit {
    std::vector<Gate> gates;      // physical qubits, with swap gates inserted
    std::vector<int> initial;     // logical -> physical at the start
    std::vector<int> final_layout;  // logical -> physical at the end
    int swaps = 0;
};

// `initial` holds circuit.n_qubits distinct physical indices < device.n_phys.
// Throws std::runtime_error if it does not.
RoutedCircuit route_circuit(const Device& dev, const Circuit& circuit, const std::vector<int>& initial);

}  // namespace qmap
