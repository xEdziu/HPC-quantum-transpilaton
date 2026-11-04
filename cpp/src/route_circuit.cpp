#include "qmap/route_circuit.hpp"

#include <stdexcept>

#include "qmap/router.hpp"

namespace qmap {

namespace {
struct SwapEvent {
    int gate_index;
    int p1;
    int p2;
};
}  // namespace

RoutedCircuit route_circuit(const Device& dev, const Circuit& circuit, const std::vector<int>& initial) {
    const int n = circuit.n_qubits;
    const int n_phys = dev.n_phys;
    if (n > n_phys) throw std::runtime_error("circuit has more qubits than the device");
    if (static_cast<int>(initial.size()) != n) throw std::runtime_error("layout length must equal the qubit count");

    std::vector<uint8_t> l2p(static_cast<size_t>(n)), p2l(static_cast<size_t>(n_phys), kEmpty);
    for (int i = 0; i < n; ++i) {
        const int p = initial[static_cast<size_t>(i)];
        if (p < 0 || p >= n_phys) throw std::runtime_error("layout entry out of range");
        if (p2l[static_cast<size_t>(p)] != kEmpty) throw std::runtime_error("layout has duplicate entries");
        l2p[static_cast<size_t>(i)] = static_cast<uint8_t>(p);
        p2l[static_cast<size_t>(p)] = static_cast<uint8_t>(i);
    }

    const std::vector<uint8_t> pairs = two_qubit_pairs(circuit);
    const int n_pairs = static_cast<int>(pairs.size() / 2);
    std::vector<SwapEvent> events;
    // route() mutates l2p/p2l; keep the initial mapping for the replay below.
    std::vector<uint8_t> cur_l2p = l2p;
    const int swaps = route(pairs.data(), n_pairs, l2p.data(), p2l.data(), dev.dist.data(), dev.next_hop.data(),
                            n_phys, [&events](int g, int a, int b) { events.push_back({g, a, b}); });

    RoutedCircuit out;
    out.initial = initial;
    out.swaps = swaps;
    out.gates.reserve(circuit.gates.size() + events.size());

    // Replay: swaps recorded for 2q gate g are emitted right before that gate.
    std::vector<uint8_t> cur_p2l(static_cast<size_t>(n_phys), kEmpty);
    for (int i = 0; i < n; ++i) cur_p2l[cur_l2p[static_cast<size_t>(i)]] = static_cast<uint8_t>(i);
    size_t next_event = 0;
    int gate_index = 0;
    for (const Gate& g : circuit.gates) {
        if (g.kind == GateKind::Cx) {
            for (; next_event < events.size() && events[next_event].gate_index == gate_index; ++next_event) {
                const int a = events[next_event].p1, b = events[next_event].p2;
                out.gates.push_back({GateKind::Swap, a, b, ""});
                const uint8_t la = cur_p2l[static_cast<size_t>(a)], lb = cur_p2l[static_cast<size_t>(b)];
                cur_p2l[static_cast<size_t>(a)] = lb;
                cur_p2l[static_cast<size_t>(b)] = la;
                if (la != kEmpty) cur_l2p[la] = static_cast<uint8_t>(b);
                if (lb != kEmpty) cur_l2p[lb] = static_cast<uint8_t>(a);
            }
            ++gate_index;
        }
        Gate phys = g;
        phys.q0 = cur_l2p[static_cast<size_t>(g.q0)];
        if (g.q1 >= 0) phys.q1 = cur_l2p[static_cast<size_t>(g.q1)];
        out.gates.push_back(std::move(phys));
    }

    out.final_layout.assign(l2p.begin(), l2p.end());
    return out;
}

}  // namespace qmap
