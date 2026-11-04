#include "qmap/qasm_io.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace qmap {

namespace {

[[noreturn]] void fail(int line_no, const std::string& msg) {
    throw std::runtime_error("qasm line " + std::to_string(line_no) + ": " + msg);
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

// Parses "q[<int>]" and returns the integer.
int parse_qubit(const std::string& s, int line_no) {
    const std::string t = trim(s);
    if (t.size() < 4 || t.compare(0, 2, "q[") != 0 || t.back() != ']') fail(line_no, "bad operand '" + t + "'");
    const std::string digits = t.substr(2, t.size() - 3);
    if (digits.empty() || digits.size() > 4 || digits.find_first_not_of("0123456789") != std::string::npos)
        fail(line_no, "bad index in '" + t + "'");
    return std::stoi(digits);
}

}  // namespace

Circuit parse_qasm(const std::string& text) {
    Circuit c;
    bool have_qreg = false;
    std::istringstream in(text);
    std::string raw;
    int line_no = 0;
    while (std::getline(in, raw)) {
        ++line_no;
        const std::string line = trim(raw);
        if (line.empty()) continue;
        if (line.back() != ';') fail(line_no, "statement must end with ';'");
        const std::string st = trim(line.substr(0, line.size() - 1));

        if (st == "OPENQASM 2.0" || st == "include \"qelib1.inc\"") continue;
        if (st.compare(0, 5, "qreg ") == 0) {
            if (have_qreg) fail(line_no, "only one qreg is supported");
            c.n_qubits = parse_qubit(st.substr(5), line_no);  // "q[n]": register must be named q
            have_qreg = true;
            continue;
        }
        if (!have_qreg) fail(line_no, "gate before qreg");

        const size_t name_end = st.find_first_of(" (");
        if (name_end == std::string::npos) fail(line_no, "malformed gate '" + st + "'");
        const std::string name = st.substr(0, name_end);
        std::string param;
        size_t operands_at = name_end;
        if (st[name_end] == '(') {
            const size_t close = st.find(')', name_end);
            if (close == std::string::npos) fail(line_no, "missing ')'");
            param = st.substr(name_end + 1, close - name_end - 1);
            operands_at = close + 1;
        }
        const std::string operands = trim(st.substr(operands_at));

        Gate g{GateKind::X, 0, -1, param};
        bool two_qubit = false;
        if (name == "rz") {
            g.kind = GateKind::Rz;
            if (param.empty()) fail(line_no, "rz needs an angle");
        } else if (name == "sx" || name == "x") {
            g.kind = name == "sx" ? GateKind::Sx : GateKind::X;
            if (!param.empty()) fail(line_no, name + " takes no parameter");
        } else if (name == "cx") {
            g.kind = GateKind::Cx;
            two_qubit = true;
            if (!param.empty()) fail(line_no, "cx takes no parameter");
        } else {
            fail(line_no, "unsupported gate '" + name + "'");
        }

        if (!two_qubit) {
            g.q0 = parse_qubit(operands, line_no);
        } else {
            const size_t comma = operands.find(',');
            if (comma == std::string::npos) fail(line_no, "cx needs two operands");
            g.q0 = parse_qubit(operands.substr(0, comma), line_no);
            g.q1 = parse_qubit(operands.substr(comma + 1), line_no);
            if (g.q0 == g.q1) fail(line_no, "cx on identical qubits");
        }
        if (g.q0 >= c.n_qubits || g.q1 >= c.n_qubits) fail(line_no, "qubit index out of range");
        c.gates.push_back(std::move(g));
    }
    if (!have_qreg) throw std::runtime_error("qasm: missing qreg");
    return c;
}

Circuit parse_qasm_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open qasm file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_qasm(ss.str());
}

std::vector<uint8_t> two_qubit_pairs(const Circuit& c) {
    std::vector<uint8_t> pairs;
    for (const Gate& g : c.gates) {
        if (g.kind != GateKind::Cx) continue;
        pairs.push_back(static_cast<uint8_t>(g.q0));
        pairs.push_back(static_cast<uint8_t>(g.q1));
    }
    return pairs;
}

std::string emit_qasm(int n_phys, const std::vector<Gate>& gates) {
    std::string out = "OPENQASM 2.0;\ninclude \"qelib1.inc\";\nqreg q[" + std::to_string(n_phys) + "];\n";
    for (const Gate& g : gates) {
        const std::string a = "q[" + std::to_string(g.q0) + "]";
        const std::string b = "q[" + std::to_string(g.q1) + "]";
        switch (g.kind) {
            case GateKind::Rz: out += "rz(" + g.param + ") " + a + ";\n"; break;
            case GateKind::Sx: out += "sx " + a + ";\n"; break;
            case GateKind::X: out += "x " + a + ";\n"; break;
            case GateKind::Cx: out += "cx " + a + "," + b + ";\n"; break;
            case GateKind::Swap: out += "swap " + a + "," + b + ";\n"; break;
        }
    }
    return out;
}

}  // namespace qmap
