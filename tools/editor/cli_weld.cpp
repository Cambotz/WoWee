#include "cli_weld.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

namespace wowee {
namespace editor {
namespace cli {

std::vector<uint32_t> buildWeldMap(
        const std::vector<glm::vec3>& positions,
        float eps,
        std::size_t& uniqueOut) {
    const float invEps = 1.0f / std::max(eps, 1e-9f);
    using QKey = std::tuple<int64_t, int64_t, int64_t>;
    std::map<QKey, uint32_t> bucket;
    std::vector<uint32_t> canon(positions.size());
    for (std::size_t v = 0; v < positions.size(); ++v) {
        const auto& p = positions[v];
        QKey k{static_cast<int64_t>(std::lround(p.x * invEps)),
               static_cast<int64_t>(std::lround(p.y * invEps)),
               static_cast<int64_t>(std::lround(p.z * invEps))};
        auto it = bucket.find(k);
        if (it == bucket.end()) {
            bucket.emplace(k, static_cast<uint32_t>(v));
            canon[v] = static_cast<uint32_t>(v);
        } else {
            canon[v] = it->second;
        }
    }
    uniqueOut = bucket.size();
    return canon;
}

EdgeStats classifyEdges(const std::vector<uint32_t>& indices,
                        const std::vector<uint32_t>& canon) {
    EdgeStats stats;
    if (indices.size() % 3 != 0) return stats;
    auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t {
        if (a > b) std::swap(a, b);
        return (uint64_t(a) << 32) | uint64_t(b);
    };
    std::unordered_map<uint64_t, uint32_t> edgeUses;
    edgeUses.reserve(indices.size());
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        uint32_t i0 = indices[t + 0];
        uint32_t i1 = indices[t + 1];
        uint32_t i2 = indices[t + 2];
        if (i0 >= canon.size() || i1 >= canon.size() ||
            i2 >= canon.size()) {
            continue;
        }
        uint32_t c0 = canon[i0], c1 = canon[i1], c2 = canon[i2];
        if (c0 != c1) ++edgeUses[edgeKey(c0, c1)];
        if (c1 != c2) ++edgeUses[edgeKey(c1, c2)];
        if (c2 != c0) ++edgeUses[edgeKey(c2, c0)];
    }
    stats.total = edgeUses.size();
    for (const auto& [_k, count] : edgeUses) {
        if (count == 1) ++stats.boundary;
        else if (count == 2) ++stats.manifold;
        else ++stats.nonManifold;
    }
    return stats;
}

} // namespace cli
} // namespace editor
} // namespace wowee
