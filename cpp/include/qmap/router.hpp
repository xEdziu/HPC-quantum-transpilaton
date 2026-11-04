#pragma once
// The single routing algorithm, shared by the host (emission, tests) and, later, the CUDA
// fitness kernel. No std containers, exceptions or heap allocation in here.
#include <cstdint>

#if defined(__CUDACC__)
#define QMAP_HD __host__ __device__
#else
#define QMAP_HD
#endif

namespace qmap {

constexpr uint8_t kEmpty = 0xFF;  // p2l entry of a physical qubit holding no logical qubit

// Greedy shortest-path router (v1, deliberately naive: no lookahead, no dedup of repeated
// pairs, no tie-breaking beyond the lowest-index next hop stored in `next_hop`).
//
//   pairs     n_pairs logical pairs (a, b), flat: pairs[2*i], pairs[2*i+1]
//   l2p       logical -> physical, in: initial layout, out: final layout
//   p2l       physical -> logical (kEmpty if unoccupied), kept consistent with l2p
//   dist      n_phys*n_phys hop distances; next_hop n_phys*n_phys first step on a shortest path
//   on_swap   called as on_swap(gate_index, p_from, p_to) for each inserted SWAP
//
// Returns the number of inserted SWAPs.
template <class OnSwap>
QMAP_HD int route(const uint8_t* pairs, int n_pairs, uint8_t* l2p, uint8_t* p2l, const uint8_t* dist,
                  const uint8_t* next_hop, int n_phys, OnSwap on_swap) {
    int count = 0;
    for (int g = 0; g < n_pairs; ++g) {
        const int a = pairs[2 * g];
        const int b = pairs[2 * g + 1];
        while (dist[l2p[a] * n_phys + l2p[b]] > 1) {
            const int pa = l2p[a];
            const int w = next_hop[pa * n_phys + l2p[b]];
            const uint8_t other = p2l[w];
            p2l[w] = static_cast<uint8_t>(a);
            p2l[pa] = other;
            l2p[a] = static_cast<uint8_t>(w);
            if (other != kEmpty) l2p[other] = static_cast<uint8_t>(pa);
            on_swap(g, pa, w);
            ++count;
        }
    }
    return count;
}

struct NoSwapCallback {
    QMAP_HD void operator()(int, int, int) const {}
};

inline QMAP_HD int count_swaps(const uint8_t* pairs, int n_pairs, uint8_t* l2p, uint8_t* p2l, const uint8_t* dist,
                               const uint8_t* next_hop, int n_phys) {
    return route(pairs, n_pairs, l2p, p2l, dist, next_hop, n_phys, NoSwapCallback{});
}

}  // namespace qmap
