#include "qmap/migration.hpp"

#include <stdexcept>

namespace qmap {

MigrationPolicy parse_migration(const std::string& s) {
    MigrationPolicy m;
    if (s.rfind("fixed:", 0) == 0) {
        size_t used = 0;
        const std::string num = s.substr(6);
        int n = 0;
        try {
            n = std::stoi(num, &used);
        } catch (const std::exception&) {
            used = 0;
        }
        if (num.empty() || used != num.size() || n < 1) throw std::invalid_argument("bad --migration '" + s + "'");
        m.scheme = Scheme::Fixed;
        m.interval = n;
        return m;
    }
    throw std::invalid_argument("bad --migration '" + s + "' (expected fixed:N)");
}

Topology parse_topology(const std::string& s) {
    if (s == "all") return Topology::All;
    if (s == "ring") return Topology::Ring;
    throw std::invalid_argument("bad --topology '" + s + "' (expected all or ring)");
}

}  // namespace qmap
