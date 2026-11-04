#pragma once
// Parser and emitter for the OpenQASM 2.0 subset used by the instances:
// rz(<expr>), sx, x, cx (and swap in routed output). Angle text is kept verbatim.
#include <cstdint>
#include <string>
#include <vector>

namespace qmap {

enum class GateKind { Rz, Sx, X, Cx, Swap };

struct Gate {
    GateKind kind;
    int q0;
    int q1;             // -1 for one-qubit gates
    std::string param;  // text between the parentheses of rz, empty otherwise
};

struct Circuit {
    int n_qubits = 0;
    std::vector<Gate> gates;
};

// Throws std::runtime_error with a line number on anything outside the subset.
Circuit parse_qasm(const std::string& text);
Circuit parse_qasm_file(const std::string& path);

// Flat array of (a, b) logical pairs, one per cx gate, in circuit order.
std::vector<uint8_t> two_qubit_pairs(const Circuit& c);

// Routed output: header with `qreg q[n_phys];`, "\n" line endings. Gates must already be physical.
std::string emit_qasm(int n_phys, const std::vector<Gate>& gates);

}  // namespace qmap
