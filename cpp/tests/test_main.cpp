// Self-contained test suite (no framework). Exit code 0 iff every CHECK passed.
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "qmap/device.hpp"
#include "qmap/qasm_io.hpp"
#include "qmap/route_circuit.hpp"
#include "qmap/router.hpp"

#ifndef QMAP_DATA_DIR
#error "QMAP_DATA_DIR must point at the repository data directory"
#endif

namespace fs = std::filesystem;
using namespace qmap;

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

// Counts swaps for logical pairs on `dev`, starting from `layout` (logical -> physical).
static std::vector<uint8_t> to_u8(const std::vector<int>& v) {
    std::vector<uint8_t> out;
    for (int x : v) out.push_back(static_cast<uint8_t>(x));
    return out;
}

static int swaps_for(const Device& dev, const std::vector<int>& pairs_int, const std::vector<int>& layout) {
    const std::vector<uint8_t> pairs = to_u8(pairs_int);
    std::vector<uint8_t> l2p = to_u8(layout);
    std::vector<uint8_t> p2l(static_cast<size_t>(dev.n_phys), kEmpty);
    for (size_t i = 0; i < layout.size(); ++i) p2l[static_cast<size_t>(layout[i])] = static_cast<uint8_t>(i);
    return count_swaps(pairs.data(), static_cast<int>(pairs.size() / 2), l2p.data(), p2l.data(), dev.dist.data(),
                       dev.next_hop.data(), dev.n_phys);
}

static Device line4() { return make_device(4, {{0, 1}, {1, 2}, {2, 3}}); }

static void test_line_graph() {
    const Device d = line4();
    CHECK(d.dist[0 * 4 + 3] == 3);
    CHECK(swaps_for(d, {0, 3}, {0, 1, 2, 3}) == 2);
    CHECK(swaps_for(d, {0, 1, 1, 2, 2, 3}, {0, 1, 2, 3}) == 0);
    CHECK(swaps_for(d, {0, 2}, {0, 1, 2, 3}) == 1);
    CHECK(swaps_for(d, {0, 2, 0, 2}, {0, 1, 2, 3}) == 1);  // second gate already adjacent
    CHECK(swaps_for(d, {0, 3, 0, 1}, {0, 1, 2, 3}) == 3);  // hand-traced: 2 swaps, then 1
    CHECK(swaps_for(d, {0, 3}, {3, 2, 1, 0}) == 2);
    CHECK(swaps_for(d, {0, 2}, {0, 1, 3}) == 2);  // 3 logical qubits, physical 2 is empty

    // Empty physical qubit: logical 0 walks 0 -> 1 -> 2, logical 1 is pushed to 0, p2l stays consistent.
    std::vector<uint8_t> l2p = to_u8({0, 1, 3}), p2l = {0, 1, kEmpty, 2};
    const uint8_t pairs[] = {0, 2};
    const int n = count_swaps(pairs, 1, l2p.data(), p2l.data(), d.dist.data(), d.next_hop.data(), 4);
    CHECK(n == 2);
    CHECK(l2p[0] == 2 && l2p[1] == 0 && l2p[2] == 3);
    CHECK(p2l[0] == 1 && p2l[1] == kEmpty && p2l[2] == 0 && p2l[3] == 2);
}

static void test_next_hop_tie_break() {
    // 4-cycle: two shortest paths 0-1-3 and 0-2-3.
    const Device d = make_device(4, {{0, 1}, {0, 2}, {1, 3}, {2, 3}});
    CHECK(d.next_hop[0 * 4 + 3] == 1);
    CHECK(d.next_hop[3 * 4 + 0] == 1);
    CHECK(d.next_hop[1 * 4 + 2] == 0);
    CHECK(d.next_hop[2 * 4 + 1] == 0);
    CHECK(d.next_hop[2 * 4 + 2] == 2);
}

static std::string data_path(const std::string& rel) { return (fs::path(QMAP_DATA_DIR) / rel).string(); }

static void test_falcon_tables() {
    const Device d = load_device(data_path("devices/falcon27.json"));
    const size_t n = static_cast<size_t>(d.n_phys);
    CHECK(d.n_phys == 27);
    for (size_t u = 0; u < n; ++u) {
        CHECK(d.dist[u * n + u] == 0 && d.next_hop[u * n + u] == u);
        for (size_t v = 0; v < n; ++v) {
            CHECK(d.dist[u * n + v] == d.dist[v * n + u]);
            if (u == v) continue;
            const size_t nh = d.next_hop[u * n + v];
            CHECK(d.dist[u * n + nh] == 1);
            CHECK(d.dist[nh * n + v] + 1 == d.dist[u * n + v]);
            for (size_t w = 0; w < nh; ++w)  // no lower-index neighbor on a shortest path
                CHECK(!(d.dist[u * n + w] == 1 && d.dist[w * n + v] + 1 == d.dist[u * n + v]));
        }
    }
}

static std::vector<fs::path> instance_files() {
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(data_path("instances")))
        if (e.path().extension() == ".qasm") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    return files;
}

static void test_parse_instances() {
    const auto files = instance_files();
    CHECK(!files.empty());
    for (const auto& f : files) {
        const Circuit c = parse_qasm_file(f.string());
        size_t text_cx = 0;
        std::ifstream in(f);
        std::string line;
        while (std::getline(in, line))
            if (line.compare(0, 3, "cx ") == 0) ++text_cx;
        size_t cx = 0;
        for (const Gate& g : c.gates) cx += g.kind == GateKind::Cx;
        CHECK(cx == text_cx);
        CHECK(two_qubit_pairs(c).size() == 2 * text_cx);
    }
    bool threw = false;
    try {
        parse_qasm("OPENQASM 2.0;\ninclude \"qelib1.inc\";\nqreg q[2];\nh q[0];\n");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

static void test_route_instances() {
    const Device d = load_device(data_path("devices/falcon27.json"));
    const size_t n_phys = static_cast<size_t>(d.n_phys);
    for (const auto& f : instance_files()) {
        const Circuit c = parse_qasm_file(f.string());
        std::vector<int> id(static_cast<size_t>(c.n_qubits));
        for (size_t i = 0; i < id.size(); ++i) id[i] = static_cast<int>(i);
        const RoutedCircuit r = route_circuit(d, c, id);

        const std::vector<uint8_t> pairs = two_qubit_pairs(c);
        const std::vector<int> pairs_int(pairs.begin(), pairs.end());
        CHECK(r.swaps == swaps_for(d, pairs_int, id));
        CHECK(r.gates.size() == c.gates.size() + static_cast<size_t>(r.swaps));

        // Replay the swaps on physical->logical and check adjacency of every 2q gate.
        std::vector<int> p2l(n_phys, -1);
        for (size_t i = 0; i < id.size(); ++i) p2l[static_cast<size_t>(id[i])] = static_cast<int>(i);
        int swaps = 0;
        for (const Gate& g : r.gates) {
            if (g.kind == GateKind::Cx || g.kind == GateKind::Swap) {
                CHECK(d.dist[static_cast<size_t>(g.q0) * n_phys + static_cast<size_t>(g.q1)] == 1);
            }
            if (g.kind == GateKind::Swap) {
                std::swap(p2l[static_cast<size_t>(g.q0)], p2l[static_cast<size_t>(g.q1)]);
                ++swaps;
            }
        }
        CHECK(swaps == r.swaps);
        std::vector<int> replayed(id.size(), -1);
        for (size_t p = 0; p < n_phys; ++p)
            if (p2l[p] >= 0) replayed[static_cast<size_t>(p2l[p])] = static_cast<int>(p);
        CHECK(replayed == r.final_layout);
        std::vector<int> sorted = r.final_layout;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
        CHECK(sorted.front() >= 0 && sorted.back() < d.n_phys);
    }
}

int main() {
    const struct {
        const char* name;
        void (*fn)();
    } cases[] = {{"line_graph", test_line_graph},
                 {"next_hop_tie_break", test_next_hop_tie_break},
                 {"falcon_tables", test_falcon_tables},
                 {"parse_instances", test_parse_instances},
                 {"route_instances", test_route_instances}};
    for (const auto& c : cases) {
        const int before = g_failures;
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::fprintf(stderr, "%s: exception: %s\n", c.name, e.what());
        }
        std::printf("%s: %s\n", c.name, g_failures == before ? "ok" : "FAILED");
    }
    return g_failures == 0 ? 0 : 1;
}
