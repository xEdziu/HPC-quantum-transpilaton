// qmap_mpi_bench: MPI ping-pong between rank 0 and every other rank, plus MPI_Barrier latency.
// CPU only (no CUDA). Run one rank per host, e.g.
//   mpiexec -genv MPICH_NETMASK <mask> -hosts 3 <ip0> 1 <ip1> 1 <ip2> 1 build\Release\qmap_mpi_bench.exe --out <csv>
// Latency is half of the measured round trip; bandwidth is bytes / half round trip (MB = 10^6 bytes).
#include <mpi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Stat {
    double median_us = 0, min_us = 0;
};

Stat summarize(std::vector<double>& seconds) {
    std::sort(seconds.begin(), seconds.end());
    const size_t n = seconds.size();
    const double med = n % 2 ? seconds[n / 2] : 0.5 * (seconds[n / 2 - 1] + seconds[n / 2]);
    return {med * 1e6, seconds.front() * 1e6};
}

// Rank 0 side: warm-up + iters round trips with `peer`; returns the half round trips in seconds.
std::vector<double> ping(std::vector<char>& buf, int bytes, int peer, int warmup, int iters) {
    std::vector<double> half;
    half.reserve(static_cast<size_t>(iters));
    for (int i = 0; i < warmup + iters; ++i) {
        const double t0 = MPI_Wtime();
        MPI_Send(buf.data(), bytes, MPI_BYTE, peer, 0, MPI_COMM_WORLD);
        MPI_Recv(buf.data(), bytes, MPI_BYTE, peer, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        const double t1 = MPI_Wtime();
        if (i >= warmup) half.push_back(0.5 * (t1 - t0));
    }
    return half;
}

void pong(std::vector<char>& buf, int bytes, int peer, int rounds) {
    for (int i = 0; i < rounds; ++i) {
        MPI_Recv(buf.data(), bytes, MPI_BYTE, peer, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Send(buf.data(), bytes, MPI_BYTE, peer, 0, MPI_COMM_WORLD);
    }
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    std::string out_path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            if (rank == 0) std::fprintf(stderr, "usage: qmap_mpi_bench [--out <csv>]\n");
            MPI_Finalize();
            return 2;
        }
    }
    if (size < 2) {
        if (rank == 0) std::fprintf(stderr, "qmap_mpi_bench needs at least 2 ranks (mpiexec -hosts 3 ...)\n");
        MPI_Finalize();
        return 2;
    }

    // Host names of all ranks, collected on rank 0.
    char name[MPI_MAX_PROCESSOR_NAME] = {};
    int name_len = 0;
    MPI_Get_processor_name(name, &name_len);
    std::vector<char> names(static_cast<size_t>(size) * MPI_MAX_PROCESSOR_NAME);
    MPI_Gather(name, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, names.data(), MPI_MAX_PROCESSOR_NAME, MPI_CHAR, 0,
               MPI_COMM_WORLD);

    const int sizes[] = {1, 8, 64, 512, 4096, 32768, 262144, 1048576};
    const int warmup = 10;
    std::vector<char> buf(1048576, 'x');

    std::ofstream csv;
    if (rank == 0) {
        std::printf("host of rank 0: %s\n%-5s %-16s %9s %6s %14s %12s %10s\n", name, "peer", "host", "bytes",
                    "iters", "lat_median_us", "lat_min_us", "bw_MBps");
        if (!out_path.empty()) {
            const std::filesystem::path parent = std::filesystem::path(out_path).parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            csv.open(out_path, std::ios::binary);
            if (!csv) {
                std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            csv << "peer_rank,peer_host,bytes,iters,lat_median_us,lat_min_us,bw_MBps\n";
        }
    }

    for (int peer = 1; peer < size; ++peer) {
        MPI_Barrier(MPI_COMM_WORLD);
        const char* peer_name = rank == 0 ? &names[static_cast<size_t>(peer) * MPI_MAX_PROCESSOR_NAME] : "";
        for (const int bytes : sizes) {
            const int iters = bytes >= 262144 ? 50 : 200;
            if (rank == 0) {
                std::vector<double> half = ping(buf, bytes, peer, warmup, iters);
                const Stat s = summarize(half);
                const double bw = bytes / (s.median_us * 1e-6) / 1e6;
                char row[256];
                std::snprintf(row, sizeof row, "%d,%s,%d,%d,%.2f,%.2f,%.2f\n", peer, peer_name, bytes, iters,
                              s.median_us, s.min_us, bw);
                std::printf("%-5d %-16s %9d %6d %14.2f %12.2f %10.2f\n", peer, peer_name, bytes, iters, s.median_us,
                            s.min_us, bw);
                if (csv.is_open()) csv << row;
            } else if (rank == peer) {
                pong(buf, bytes, 0, warmup + iters);
            }
        }
    }

    // MPI_Barrier over all ranks, as seen by rank 0; row peer_rank = -1.
    constexpr int kBarriers = 100;
    MPI_Barrier(MPI_COMM_WORLD);
    std::vector<double> bt;
    for (int i = 0; i < kBarriers; ++i) {
        const double t0 = MPI_Wtime();
        MPI_Barrier(MPI_COMM_WORLD);
        bt.push_back(MPI_Wtime() - t0);
    }
    if (rank == 0) {
        const Stat s = summarize(bt);
        std::printf("MPI_Barrier over %d ranks: median %.2f us, min %.2f us (%d calls)\n", size, s.median_us, s.min_us,
                    kBarriers);
        if (csv.is_open()) {
            char row[256];
            std::snprintf(row, sizeof row, "-1,barrier_all,0,%d,%.2f,%.2f,0\n", kBarriers, s.median_us, s.min_us);
            csv << row;
        }
    }
    MPI_Finalize();
    return 0;
}
