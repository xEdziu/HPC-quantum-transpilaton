#pragma once
// Migration settings shared by the MPI island layer and its unit tests. No MPI dependency.
#include <string>

namespace qmap {

enum class Topology { All, Ring };

// Migration interval policy, in generations. Adaptive schemes: Mambrini & Sudholt, "Design and
// Analysis of Schemes for Adapting Migration Intervals in Parallel Evolutionary Algorithms",
// Evolutionary Computation 23(4), 2015. Both start at tau = 1.
//   A: a strict improvement of the island best (local or immigrant) is broadcast at once and resets
//      tau to 1; a period of tau generations without improvement ends with a migration and tau *= 2.
//   B: migrate only at the end of each period of tau generations; then tau = ceil(tau / 2) if there
//      was a strict improvement during the period, else tau *= 2.
enum class Scheme { Fixed, A, B };

struct MigrationPolicy {
    Scheme scheme = Scheme::Fixed;
    int interval = 100;  // Fixed: the interval; adaptive schemes: unused (they start at 1)
};

// Interval after a period ends (A: also right after an improvement). Fixed returns tau.
// The result is capped at `cap` (>= 1) so doubling cannot overflow; B never goes below 1.
int next_interval(Scheme scheme, int tau, bool improved, int cap);

// "fixed:N", "adaptiveA" or "adaptiveB". Throws std::invalid_argument on bad input.
MigrationPolicy parse_migration(const std::string& s);

// "all" or "ring". Throws std::invalid_argument on bad input.
Topology parse_topology(const std::string& s);

}  // namespace qmap
