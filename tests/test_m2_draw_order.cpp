// The order the world's models are drawn in.
//
// Instancing wants every entry sharing a model adjacent; a tile renderer
// shading cutout foliage wants the near trees first, so the far ones are
// rejected against the depth they laid down. Both at once is the whole point
// of this sort, and a change that quietly gave up either one would not show up
// as anything but a frame time.

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "rendering/m2_draw_order.hpp"

using wowee::rendering::sortModelGroupsFrontToBack;

namespace {

struct Entry {
    uint32_t index = 0;
    uint32_t modelId = 0;
    float distSq = 0.0f;
};

/// Every model's entries must form one unbroken run, or the group is drawn as
/// two draws instead of one and instancing has been given up.
bool groupsAreContiguous(const std::vector<Entry>& entries) {
    std::unordered_set<uint32_t> seen;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0 && entries[i].modelId == entries[i - 1].modelId) continue;
        if (!seen.insert(entries[i].modelId).second) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("groups are drawn nearest first and stay contiguous") {
    // Three models interleaved, the nearest instance of each far apart: model
    // 7 is closest, then 3, then 1 - which is neither the id order nor the
    // order they were added in.
    std::vector<Entry> entries = {
        {.index = 0, .modelId = 1, .distSq = 900.0f},
        {.index = 1, .modelId = 3, .distSq = 400.0f},
        {.index = 2, .modelId = 7, .distSq = 100.0f},
        {.index = 3, .modelId = 1, .distSq = 1600.0f},
        {.index = 4, .modelId = 3, .distSq = 2500.0f},
        {.index = 5, .modelId = 7, .distSq = 3600.0f},
    };

    sortModelGroupsFrontToBack(entries);

    REQUIRE(entries.size() == 6);
    CHECK(groupsAreContiguous(entries));
    CHECK(entries[0].modelId == 7);
    CHECK(entries[1].modelId == 7);
    CHECK(entries[2].modelId == 3);
    CHECK(entries[3].modelId == 3);
    CHECK(entries[4].modelId == 1);
    CHECK(entries[5].modelId == 1);
}

TEST_CASE("a group goes where its nearest instance is, not its average") {
    // Model 2 has one instance right in front of the camera and a thousand far
    // away; model 5 sits at a middling distance. The near instance is the one
    // that has to lay depth down first, so model 2 leads.
    std::vector<Entry> entries = {{.index = 0, .modelId = 2, .distSq = 1.0f}};
    for (uint32_t i = 0; i < 1000; ++i) {
        entries.push_back({.index = i + 1, .modelId = 2, .distSq = 90000.0f});
    }
    entries.push_back({.index = 1001, .modelId = 5, .distSq = 2500.0f});

    sortModelGroupsFrontToBack(entries);

    CHECK(entries.front().modelId == 2);
    CHECK(entries.back().modelId == 5);
    CHECK(groupsAreContiguous(entries));
}

TEST_CASE("nothing is dropped, duplicated or invented") {
    std::vector<Entry> entries;
    for (uint32_t i = 0; i < 500; ++i) {
        entries.push_back({.index = i,
                           .modelId = i % 17,
                           .distSq = static_cast<float>((i * 37) % 911)});
    }
    const std::size_t before = entries.size();

    sortModelGroupsFrontToBack(entries);

    REQUIRE(entries.size() == before);
    std::unordered_set<uint32_t> indices;
    for (const Entry& e : entries) CHECK(indices.insert(e.index).second);
    CHECK(indices.size() == before);
    CHECK(groupsAreContiguous(entries));
}

TEST_CASE("the empty and single-entry cases are left alone") {
    std::vector<Entry> none;
    sortModelGroupsFrontToBack(none);
    CHECK(none.empty());

    std::vector<Entry> one = {{.index = 4, .modelId = 9, .distSq = 12.0f}};
    sortModelGroupsFrontToBack(one);
    REQUIRE(one.size() == 1);
    CHECK(one[0].index == 4);
}
