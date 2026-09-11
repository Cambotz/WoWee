#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace wowee::rendering {

/// The order the world's models are drawn in, and why it is not just whichever
/// order batches them best.
///
/// Instancing wants everything sharing a model adjacent, so the opaque pass
/// sorted on model id alone and drew the groups in whatever order the ids
/// happened to fall. For opaque geometry on a tile renderer that costs nothing
/// - the hardware resolves visibility before it shades. Foliage is not opaque
/// geometry. Leaves are drawn through the cutout pipeline, where coverage
/// comes out of the fragment shader, so the shader has to run before the
/// hardware knows whether the fragment survives. A forest drawn in model-id
/// order therefore shades every leaf of every tree behind every other tree.
///
/// Ordering the groups by their nearest instance fixes that without giving up
/// instancing: the near trees lay down depth first and the far ones are
/// rejected against it. Within a group the entries keep their model id, so
/// every group is still one run of instances and one set of buffer binds.
///
/// Nothing here changes what is drawn, only when - the cutout pipeline has
/// blending disabled, so the result does not depend on the order.
template <typename Entry>
void sortModelGroupsFrontToBack(std::vector<Entry>& entries) {
    if (entries.size() < 2) return;

    // The nearest instance of each model. That, rather than any one instance's
    // own distance, is the group's place in the order: the group is drawn as a
    // unit, so it goes where its closest member would.
    std::unordered_map<uint32_t, float> nearest;
    nearest.reserve(entries.size() / 4 + 1);
    for (const Entry& e : entries) {
        auto [it, inserted] = nearest.try_emplace(e.modelId, e.distSq);
        if (!inserted && e.distSq < it->second) it->second = e.distSq;
    }

    // One lookup per entry rather than one or two per comparison: the sort is
    // over tens of thousands of entries and a hash probe inside the comparator
    // is the kind of thing that turns a cheap sort into a profile entry.
    std::vector<std::pair<float, uint32_t>> key;
    key.resize(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        key[i] = {nearest[entries[i].modelId], entries[i].modelId};
    }

    std::vector<uint32_t> order(entries.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<uint32_t>(i);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return key[a] < key[b];
    });

    std::vector<Entry> sorted;
    sorted.reserve(entries.size());
    for (uint32_t i : order) sorted.push_back(entries[i]);
    entries.swap(sorted);
}

}  // namespace wowee::rendering
