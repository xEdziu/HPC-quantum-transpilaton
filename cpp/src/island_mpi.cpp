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

    void send_stop() {
        for (int dest = 0; dest < size; ++dest) {
            if (dest == rank) continue;
            pending.emplace_back();
            Pending& p = pending.back();
            p.buf.resize(1);  // payload is empty (count 0); keep a valid pointer anyway
            MPI_Isend(p.buf.data(), 0, MPI_BYTE, dest, kTagStop, MPI_COMM_WORLD, &p.req);
            ++sent_to[static_cast<size_t>(dest)];
        }
    }

    // Receive and discard the messages that are still on their way (counts come from the Alltoall).
    void drain(const std::vector<int>& sent_to_me) {
        std::vector<uint8_t> buf(message_bytes());
        for (int src = 0; src < size; ++src) {
            for (int left = sent_to_me[static_cast<size_t>(src)] - received_from[static_cast<size_t>(src)]; left > 0;
                 --left) {
                MPI_Status st;
                MPI_Recv(buf.data(), static_cast<int>(buf.size()), MPI_BYTE, src, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
                ++received_from[static_cast<size_t>(src)];
                if (st.MPI_TAG == kTagMigrant) ++stats.migrants_received;
            }
        }
    }

    GlobalResult finish(GaResult& local) {
        push("mpi_terminate");
        if (local.stop_reason == "external") local.stop_reason = "remote_stop";
        if (local.stop_reason == "target") send_stop();  // a)

        // b) Tell every rank how many messages it will receive from us.
        std::vector<int> sent_to_me(static_cast<size_t>(size));
        MPI_Alltoall(sent_to.data(), 1, MPI_INT, sent_to_me.data(), 1, MPI_INT, MPI_COMM_WORLD);
        // c) Receive exactly what is still outstanding, then d) wait for our own sends.
        drain(sent_to_me);
        std::vector<MPI_Request> reqs;
        for (const Pending& p : pending) reqs.push_back(p.req);
        MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);
        pending.clear();

        // e) Reduce the results.
        GlobalResult g;
        struct {
            int value, rank;
        } mine{local.best_fitness, rank}, best{};
        MPI_Allreduce(&mine, &best, 1, MPI_2INT, MPI_MINLOC, MPI_COMM_WORLD);
        g.best_fitness = best.value;
        g.best_rank = best.rank;
        g.best = local.best;
        g.best.resize(static_cast<size_t>(n_phys));
        MPI_Bcast(g.best.data(), n_phys, MPI_BYTE, g.best_rank, MPI_COMM_WORLD);

        const double no_ttt = 1e300;
        const double my_ttt = local.ttt_s >= 0 ? local.ttt_s : no_ttt;
        double ttt = no_ttt, time = 0;
        long long evals = 0;
        MPI_Reduce(&my_ttt, &ttt, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
        MPI_Reduce(&local.evals, &evals, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&local.total_time_s, &time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        g.ttt_s = ttt < no_ttt ? ttt : -1;
        g.evals = evals;
        g.time_s = time;
        pop();
        return g;
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

GlobalResult Island::finish(GaResult& local) { return impl_->finish(local); }

const IslandStats& Island::stats() const { return impl_->stats; }

}  // namespace qmap
