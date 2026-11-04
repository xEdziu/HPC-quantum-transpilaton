#pragma once
// Device coupling graph with precomputed all-pairs distances and next hops.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace qmap {

struct Device {
    int n_phys = 0;
    std::vector<std::pair<int, int>> edges;  // undirected, a < b
    std::vector<uint8_t> dist;               // n_phys * n_phys, BFS hop counts
    // next_hop[u*n+v]: lowest-index neighbor w of u with dist[w][v] == dist[u][v]-1;
    // next_hop[u*n+u] == u.
    std::vector<uint8_t> next_hop;
};

// Builds dist and next_hop from an edge list. Throws std::runtime_error if the graph is
// disconnected or n_phys is out of range for uint8_t tables.
Device make_device(int n_phys, const std::vector<std::pair<int, int>>& edges);

// Loads data/devices/*.json (keys n_phys, edges, distance). Throws on any inconsistency,
// including a mismatch between the computed distances and the JSON `distance` matrix.
Device load_device(const std::string& path);

}  // namespace qmap
