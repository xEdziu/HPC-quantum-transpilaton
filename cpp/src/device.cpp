#include "qmap/device.hpp"

#include <algorithm>
#include <fstream>
#include <queue>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace qmap {

namespace {
constexpr uint8_t kUnreachable = 0xFF;
}

Device make_device(int n_phys, const std::vector<std::pair<int, int>>& edges) {
    if (n_phys <= 0 || n_phys > 254) throw std::runtime_error("n_phys must be in 1..254");
    Device d;
    d.n_phys = n_phys;
    d.edges = edges;
    const size_t n = static_cast<size_t>(n_phys);
    std::vector<std::vector<int>> adj(n);
    for (const auto& e : edges) {
        if (e.first < 0 || e.second < 0 || e.first >= n_phys || e.second >= n_phys || e.first == e.second)
            throw std::runtime_error("invalid edge in device");
        adj[static_cast<size_t>(e.first)].push_back(e.second);
        adj[static_cast<size_t>(e.second)].push_back(e.first);
    }
    for (auto& a : adj) std::sort(a.begin(), a.end());

    d.dist.assign(n * n, kUnreachable);
    for (size_t s = 0; s < n; ++s) {
        std::queue<int> q;
        d.dist[s * n + s] = 0;
        q.push(static_cast<int>(s));
        while (!q.empty()) {
            const size_t u = static_cast<size_t>(q.front());
            q.pop();
            for (int w : adj[u]) {
                uint8_t& dw = d.dist[s * n + static_cast<size_t>(w)];
                if (dw == kUnreachable) {
                    dw = static_cast<uint8_t>(d.dist[s * n + u] + 1);
                    q.push(w);
                }
            }
        }
    }
    for (uint8_t x : d.dist)
        if (x == kUnreachable) throw std::runtime_error("device graph is disconnected");

    d.next_hop.assign(n * n, 0);
    for (size_t u = 0; u < n; ++u) {
        for (size_t v = 0; v < n; ++v) {
            if (u == v) {
                d.next_hop[u * n + v] = static_cast<uint8_t>(u);
                continue;
            }
            // adj[u] is sorted, so the first match is the lowest-index neighbor.
            for (int w : adj[u]) {
                if (d.dist[static_cast<size_t>(w) * n + v] + 1 == d.dist[u * n + v]) {
                    d.next_hop[u * n + v] = static_cast<uint8_t>(w);
                    break;
                }
            }
        }
    }
    return d;
}

Device load_device(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open device file: " + path);
    const nlohmann::json j = nlohmann::json::parse(f);
    const int n_phys = j.at("n_phys").get<int>();
    std::vector<std::pair<int, int>> edges;
    for (const auto& e : j.at("edges")) edges.emplace_back(e.at(0).get<int>(), e.at(1).get<int>());
    Device d = make_device(n_phys, edges);

    const auto& dm = j.at("distance");
    if (dm.size() != static_cast<size_t>(n_phys)) throw std::runtime_error("distance matrix has wrong size");
    for (int u = 0; u < n_phys; ++u) {
        const auto& row = dm.at(static_cast<size_t>(u));
        if (row.size() != static_cast<size_t>(n_phys)) throw std::runtime_error("distance matrix has wrong row size");
        for (int v = 0; v < n_phys; ++v) {
            const int expected = row.at(static_cast<size_t>(v)).get<int>();
            if (d.dist[static_cast<size_t>(u * n_phys + v)] != expected)
                throw std::runtime_error("computed distance differs from JSON distance matrix");
        }
    }
    return d;
}

}  // namespace qmap
