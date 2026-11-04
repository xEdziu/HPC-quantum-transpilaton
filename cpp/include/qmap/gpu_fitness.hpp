#pragma once
// Batched GPU fitness (v1, deliberately naive): one thread per individual, all data in global
// memory, synchronous pageable copies on every evaluate(). Plain C++ header: no CUDA includes.
#include <cstdint>
#include <vector>

#include "qmap/device.hpp"

namespace qmap {

class GpuFitness {
public:
    // pairs: flat (a, b) logical pairs as returned by two_qubit_pairs(). Uploads pairs, dist and
    // next_hop once; allocates device buffers for up to max_pop individuals.
    // Throws std::runtime_error on CUDA errors or n_phys > 32.
    GpuFitness(const Device& dev, const std::vector<uint8_t>& pairs, int n_logical, int max_pop);
    ~GpuFitness();
    GpuFitness(const GpuFitness&) = delete;
    GpuFitness& operator=(const GpuFitness&) = delete;

    // pop: pop_size * n_phys genes (each row a permutation of the physical qubits).
    // out: pop_size SWAP counts. Throws if pop_size > max_pop.
    void evaluate(const uint8_t* pop, int pop_size, int* out);

private:
    int n_phys_, n_logical_, n_pairs_, max_pop_;
    uint8_t* d_pop_ = nullptr;
    uint8_t* d_pairs_ = nullptr;
    uint8_t* d_dist_ = nullptr;
    uint8_t* d_next_hop_ = nullptr;
    int* d_out_ = nullptr;
};

}  // namespace qmap
