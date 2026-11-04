#pragma once
// Migration settings shared by the MPI island layer and its unit tests. No MPI dependency.
#include <string>

namespace qmap {

enum class Topology { All, Ring };

// Migration interval policy, in generations: fixed, or adapted at run time.
enum class Scheme { Fixed };

struct MigrationPolicy {
    Scheme scheme = Scheme::Fixed;
    int interval = 100;  // Fixed: the interval; adaptive schemes: unused (they start at 1)
};

// "fixed:N". Throws std::invalid_argument on bad input.
MigrationPolicy parse_migration(const std::string& s);

// "all" or "ring". Throws std::invalid_argument on bad input.
Topology parse_topology(const std::string& s);

}  // namespace qmap
