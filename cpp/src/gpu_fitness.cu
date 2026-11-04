#include <nvtx3/nvToolsExt.h>

#include <cuda_runtime.h>

#include <stdexcept>
#include <string>

#include "qmap/gpu_fitness.hpp"
#include "qmap/router.hpp"

#define QMAP_CUDA_CHECK(call)                                                                   \
    do {                                                                                        \
        const cudaError_t err_ = (call);                                                        \
        if (err_ != cudaSuccess)                                                                \
            throw std::runtime_error(std::string(cudaGetErrorString(err_)) + " at " + __FILE__ + \
                                     ":" + std::to_string(__LINE__));                           \
    } while (0)

namespace qmap {

namespace {

constexpr int kMaxPhys = 32;
constexpr int kBlock = 256;

// One thread = one individual. Everything is read from global memory.
__global__ void fitness_kernel(const uint8_t* pop, int pop_size, const uint8_t* pairs, int n_pairs,
                               const uint8_t* dist, const uint8_t* next_hop, int n_phys, int n_logical,
                               int* out) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pop_size) return;

    uint8_t l2p[kMaxPhys];
    uint8_t p2l[kMaxPhys];
    for (int p = 0; p < n_phys; ++p) p2l[p] = kEmpty;
    for (int i = 0; i < n_logical; ++i) {
        const uint8_t g = pop[idx * n_phys + i];
        l2p[i] = g;
        p2l[g] = static_cast<uint8_t>(i);
    }
    out[idx] = count_swaps(pairs, n_pairs, l2p, p2l, dist, next_hop, n_phys);
}

}  // namespace

GpuFitness::GpuFitness(const Device& dev, const std::vector<uint8_t>& pairs, int n_logical, int max_pop)
    : n_phys_(dev.n_phys), n_logical_(n_logical), n_pairs_(static_cast<int>(pairs.size() / 2)), max_pop_(max_pop) {
    if (n_phys_ > kMaxPhys) throw std::runtime_error("GpuFitness: n_phys exceeds 32");
    if (n_logical_ > n_phys_) throw std::runtime_error("GpuFitness: n_logical exceeds n_phys");
    try {
        const size_t tbl = static_cast<size_t>(n_phys_) * n_phys_;
        QMAP_CUDA_CHECK(cudaMalloc(&d_pop_, static_cast<size_t>(max_pop_) * n_phys_));
        QMAP_CUDA_CHECK(cudaMalloc(&d_pairs_, pairs.size() ? pairs.size() : 1));
        QMAP_CUDA_CHECK(cudaMalloc(&d_dist_, tbl));
        QMAP_CUDA_CHECK(cudaMalloc(&d_next_hop_, tbl));
        QMAP_CUDA_CHECK(cudaMalloc(&d_out_, static_cast<size_t>(max_pop_) * sizeof(int)));
        QMAP_CUDA_CHECK(cudaMemcpy(d_pairs_, pairs.data(), pairs.size(), cudaMemcpyHostToDevice));
        QMAP_CUDA_CHECK(cudaMemcpy(d_dist_, dev.dist.data(), tbl, cudaMemcpyHostToDevice));
        QMAP_CUDA_CHECK(cudaMemcpy(d_next_hop_, dev.next_hop.data(), tbl, cudaMemcpyHostToDevice));
    } catch (...) {
        cudaFree(d_pop_);
        cudaFree(d_pairs_);
        cudaFree(d_dist_);
        cudaFree(d_next_hop_);
        cudaFree(d_out_);
        throw;
    }
}

GpuFitness::~GpuFitness() {
    cudaFree(d_pop_);
    cudaFree(d_pairs_);
    cudaFree(d_dist_);
    cudaFree(d_next_hop_);
    cudaFree(d_out_);
}

void GpuFitness::evaluate(const uint8_t* pop, int pop_size, int* out) {
    if (pop_size < 0 || pop_size > max_pop_) throw std::runtime_error("GpuFitness: pop_size exceeds max_pop");
    if (pop_size == 0) return;

    nvtxRangePushA("fitness_h2d");
    cudaError_t err = cudaMemcpy(d_pop_, pop, static_cast<size_t>(pop_size) * n_phys_, cudaMemcpyHostToDevice);
    nvtxRangePop();
    QMAP_CUDA_CHECK(err);

    nvtxRangePushA("fitness_kernel");
    const int grid = (pop_size + kBlock - 1) / kBlock;
    fitness_kernel<<<grid, kBlock>>>(d_pop_, pop_size, d_pairs_, n_pairs_, d_dist_, d_next_hop_, n_phys_, n_logical_,
                                     d_out_);
    err = cudaGetLastError();
    if (err == cudaSuccess) err = cudaDeviceSynchronize();
    nvtxRangePop();
    QMAP_CUDA_CHECK(err);

    nvtxRangePushA("fitness_d2h");
    err = cudaMemcpy(out, d_out_, static_cast<size_t>(pop_size) * sizeof(int), cudaMemcpyDeviceToHost);
    nvtxRangePop();
    QMAP_CUDA_CHECK(err);
}

}  // namespace qmap
