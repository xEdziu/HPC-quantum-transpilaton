#include "qmap/island_mpi.hpp"

#include <mpi.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace qmap {

namespace {
constexpr int kTagMigrant = 1;
constexpr int kTagStop = 2;
}  // namespace

MpiSession::MpiSession(int& argc, char**& argv) {
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        throw std::runtime_error("MPI does not provide MPI_THREAD_FUNNELED");
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank_);
    MPI_Comm_size(MPI_COMM_WORLD, &size_);
}

MpiSession::~MpiSession() { MPI_Finalize(); }

void MpiSession::abort(int code) {
    MPI_Abort(MPI_COMM_WORLD, code);
    std::abort();  // not reached
}

uint64_t rank_seed(uint64_t seed, int rank) {
    return splitmix64(seed ^ (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(rank + 1)));
}

// One migrant: n_phys genes followed by the int fitness (native byte order; same-architecture hosts).
// Every outstanding Isend owns its heap buffer until the request completes.
struct Island::Impl {
    struct Pending {
        MPI_Request req = MPI_REQUEST_NULL;
        std::vector<uint8_t> buf;
    };

    int n_phys, rank = 0, size = 1;
    Topology topology;
    MigrationPolicy migration;
    int max_generations;
    std::function<void(const char*)> range_push;
    std::function<void()> range_pop;

    std::vector<Pending> pending;
    std::vector<int> sent_to;         // messages (migrants + stops) posted per destination
    std::vector<int> received_from;   // messages received per source
    IslandStats stats;
    bool stop_requested = false;

    int best_fitness = INT_MAX;       // best-so-far of this island, including immigrants
    std::vector<uint8_t> best_genes;
    int last_migration_gen = 0;

    size_t message_bytes() const { return static_cast<size_t>(n_phys) + sizeof(int); }

    void push(const char* name) {
        if (range_push) range_push(name);
    }
    void pop() {
        if (range_pop) range_pop();
    }

    std::vector<int> destinations() const {
        std::vector<int> d;
        if (topology == Topology::All) {
            for (int r = 0; r < size; ++r)
                if (r != rank) d.push_back(r);
        } else if (size > 1) {
            d.push_back((rank + 1) % size);
        }
        return d;
    }

    // Fresh heap buffer and one Isend per destination.
    void send_migrant() {
        for (const int dest : destinations()) {
            pending.emplace_back();
            Pending& p = pending.back();
            p.buf.resize(message_bytes());
            std::memcpy(p.buf.data(), best_genes.data(), static_cast<size_t>(n_phys));
            std::memcpy(p.buf.data() + n_phys, &best_fitness, sizeof(int));
            MPI_Isend(p.buf.data(), static_cast<int>(p.buf.size()), MPI_BYTE, dest, kTagMigrant, MPI_COMM_WORLD,
                      &p.req);
            ++sent_to[static_cast<size_t>(dest)];
            ++stats.migrants_sent;
        }
    }

    // Erase the sends that have completed.
    void reap() {
        pending.erase(std::remove_if(pending.begin(), pending.end(),
                                     [](Pending& p) {
                                         int done = 0;
                                         MPI_Test(&p.req, &done, MPI_STATUS_IGNORE);
                                         return done != 0;
                                     }),
                      pending.end());
    }

    // Receive everything that has arrived; immigrants overwrite the worst individual.
    void receive_all(IslandView& view) {
        std::vector<uint8_t> buf(message_bytes());
        for (;;) {
            int flag = 0;
            MPI_Status st;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &st);
            if (!flag) break;
            MPI_Recv(buf.data(), static_cast<int>(buf.size()), MPI_BYTE, st.MPI_SOURCE, st.MPI_TAG, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            ++received_from[static_cast<size_t>(st.MPI_SOURCE)];
            if (st.MPI_TAG == kTagStop) {
                stop_requested = true;
                continue;
            }
            int fitness = 0;
            std::memcpy(&fitness, buf.data() + n_phys, sizeof(int));
            view.immigrate(buf.data(), fitness);
            ++stats.migrants_received;
            if (fitness < best_fitness) {
                best_fitness = fitness;
                best_genes.assign(buf.begin(), buf.begin() + n_phys);
            }
        }
    }

    bool on_generation(IslandView& view) {
        push("mpi_migrate");
        reap();
        if (view.best_fitness() < best_fitness) {
            best_fitness = view.best_fitness();
            best_genes.assign(view.best_genes(), view.best_genes() + n_phys);
        }
        receive_all(view);
        if (view.generation() - last_migration_gen >= stats.interval) {
            send_migrant();
            last_migration_gen = view.generation();
        }
        pop();
        return !stop_requested;
    }
};

Island::Island(int n_phys, Topology topology, MigrationPolicy migration, int max_generations,
               std::function<void(const char*)> range_push, std::function<void()> range_pop)
    : impl_(new Impl) {
    MPI_Comm_rank(MPI_COMM_WORLD, &impl_->rank);
    MPI_Comm_size(MPI_COMM_WORLD, &impl_->size);
    impl_->n_phys = n_phys;
    impl_->topology = topology;
    impl_->migration = migration;
    impl_->max_generations = max_generations;
    impl_->range_push = std::move(range_push);
    impl_->range_pop = std::move(range_pop);
    impl_->sent_to.assign(static_cast<size_t>(impl_->size), 0);
    impl_->received_from.assign(static_cast<size_t>(impl_->size), 0);
    impl_->stats.interval = migration.interval;
}

Island::~Island() = default;

int Island::rank() const { return impl_->rank; }
int Island::size() const { return impl_->size; }

void Island::synchronize_start() { MPI_Barrier(MPI_COMM_WORLD); }

IslandHook Island::hook() {
    return [this](IslandView& view) { return impl_->on_generation(view); };
}

const IslandStats& Island::stats() const { return impl_->stats; }

}  // namespace qmap
