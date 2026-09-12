// Tests for TransportPathRepository (Phase 3 of spline refactoring).
// Verifies PathEntry wrapping, path operations, and CatmullRomSpline integration.
#include <catch2/catch_amalgamated.hpp>
#include "game/transport_path_repository.hpp"
#include "pipeline/asset_manager.hpp"
#include "math/spline.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>

// ── Minimal stubs for pipeline symbols referenced by DBC loading functions ──
// The test never calls loadTransportAnimationDBC / loadTaxiPathNodeDBC,
// but the linker still needs these symbols from the compiled translation unit.
namespace wowee::pipeline {
AssetManager::AssetManager() {}
AssetManager::~AssetManager() {}
std::vector<uint8_t> AssetManager::readFile(const std::string&) const { return {}; }
std::vector<uint8_t> AssetManager::readFileOptional(const std::string&) const { return {}; }
} // namespace wowee::pipeline

using namespace wowee;

// ── Helpers ────────────────────────────────────────────────────

static constexpr float kEps = 0.001f;

static void requireVec3Near(const glm::vec3& v, float x, float y, float z,
                             float eps = kEps) {
    REQUIRE(std::abs(v.x - x) < eps);
    REQUIRE(std::abs(v.y - y) < eps);
    REQUIRE(std::abs(v.z - z) < eps);
}

// ── PathEntry construction ─────────────────────────────────────

TEST_CASE("PathEntry wraps CatmullRomSpline with metadata", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {10.0f, 0.0f, 0.0f}},
        {2000, {20.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 42, false, true, false);

    REQUIRE(entry.pathId == 42);
    REQUIRE(entry.fromDBC == true);
    REQUIRE(entry.zOnly == false);
    REQUIRE(entry.worldCoords == false);
    REQUIRE(entry.spline.keyCount() == 3);
    REQUIRE(entry.spline.durationMs() == 2000);
}

TEST_CASE("PathEntry is move-constructible", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {500, {5.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 99, true, false, true);

    game::PathEntry moved(std::move(entry));
    REQUIRE(moved.pathId == 99);
    REQUIRE(moved.zOnly == true);
    REQUIRE(moved.worldCoords == true);
    REQUIRE(moved.spline.keyCount() == 2);
}

// ── Repository: loadPathFromNodes ──────────────────────────────

TEST_CASE("loadPathFromNodes stores a retrievable path", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    std::vector<glm::vec3> waypoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 0.0f},
        {200.0f, 0.0f, 0.0f},
    };

    repo.loadPathFromNodes(1001, waypoints, true, 10.0f);
    auto* entry = repo.findPath(1001);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->pathId == 1001);
    REQUIRE(entry->fromDBC == false);
    REQUIRE(entry->zOnly == false);
    // 3 waypoints + 1 wrap point = 4 keys
    REQUIRE(entry->spline.keyCount() == 4);
    REQUIRE(entry->spline.durationMs() > 0);
}

TEST_CASE("loadPathFromNodes single waypoint creates stationary path", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    repo.loadPathFromNodes(2001, {{5.0f, 6.0f, 7.0f}}, false, 10.0f);

    auto* entry = repo.findPath(2001);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->spline.keyCount() == 1);
    // Stationary: duration is the last key's time (0ms for single key)
    requireVec3Near(entry->spline.evaluatePosition(0), 5.0f, 6.0f, 7.0f);
}

TEST_CASE("loadPathFromNodes non-looping omits wrap point", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    std::vector<glm::vec3> waypoints = {
        {0.0f, 0.0f, 0.0f},
        {50.0f, 0.0f, 0.0f},
    };

    repo.loadPathFromNodes(3001, waypoints, false, 10.0f);
    auto* entry = repo.findPath(3001);
    REQUIRE(entry != nullptr);
    // Non-looping: 2 waypoints, no wrap point
    REQUIRE(entry->spline.keyCount() == 2);
}

TEST_CASE("loadPathFromNodes looping adds wrap point", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    std::vector<glm::vec3> waypoints = {
        {0.0f, 0.0f, 0.0f},
        {50.0f, 0.0f, 0.0f},
    };

    repo.loadPathFromNodes(3002, waypoints, true, 10.0f);
    auto* entry = repo.findPath(3002);
    REQUIRE(entry != nullptr);
    // Looping: 2 waypoints + 1 wrap point = 3 keys
    REQUIRE(entry->spline.keyCount() == 3);
    // Wrap point should match first waypoint
    const auto& keys = entry->spline.keys();
    requireVec3Near(keys.back().position, 0.0f, 0.0f, 0.0f);
}

// ── Repository: findPath / storePath / hasPathForEntry ──────────

TEST_CASE("findPath returns nullptr for missing paths", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    REQUIRE(repo.findPath(999) == nullptr);
}

TEST_CASE("storePath overwrites existing paths", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Store first path
    std::vector<math::SplineKey> keys1 = {{0, {0, 0, 0}}, {1000, {10, 0, 0}}};
    math::CatmullRomSpline spline1(std::move(keys1), false);
    repo.storePath(500, game::PathEntry(std::move(spline1), 500, false, true, false));

    auto* e1 = repo.findPath(500);
    REQUIRE(e1 != nullptr);
    REQUIRE(e1->spline.keyCount() == 2);

    // Overwrite with different path
    std::vector<math::SplineKey> keys2 = {{0, {0, 0, 0}}, {500, {5, 0, 0}}, {1000, {10, 5, 0}}};
    math::CatmullRomSpline spline2(std::move(keys2), false);
    repo.storePath(500, game::PathEntry(std::move(spline2), 500, true, false, true));

    auto* e2 = repo.findPath(500);
    REQUIRE(e2 != nullptr);
    REQUIRE(e2->spline.keyCount() == 3);
    REQUIRE(e2->zOnly == true);
    REQUIRE(e2->worldCoords == true);
}

TEST_CASE("hasPathForEntry checks fromDBC flag", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Non-DBC path
    repo.loadPathFromNodes(700, {{0, 0, 0}, {10, 0, 0}}, true, 10.0f);
    REQUIRE(repo.hasPathForEntry(700) == false);  // fromDBC=false

    // DBC path (via storePath)
    std::vector<math::SplineKey> keys = {{0, {0, 0, 0}}, {1000, {10, 0, 0}}};
    math::CatmullRomSpline spline(std::move(keys), false);
    repo.storePath(701, game::PathEntry(std::move(spline), 701, false, true, false));
    REQUIRE(repo.hasPathForEntry(701) == true);
}

TEST_CASE("loadPathFromNodes replaces stale runtime paths but preserves DBC paths",
          "[transport_path_repo][expansion]") {
    game::TransportPathRepository repo;

    repo.loadPathFromNodes(710, {{0, 0, 0}, {10, 0, 0}}, false, 10.0f);
    repo.loadPathFromNodes(710, {{0, 0, 0}, {20, 0, 0}, {40, 0, 0}}, false, 10.0f);
    REQUIRE(repo.findPath(710)->spline.keyCount() == 3);

    std::vector<math::SplineKey> dbcKeys = {
        {0, {0, 0, 0}}, {1000, {50, 0, 0}}
    };
    math::CatmullRomSpline dbcSpline(std::move(dbcKeys), false);
    repo.storePath(711, game::PathEntry(std::move(dbcSpline), 711, false, true, false));

    repo.loadPathFromNodes(711, {{1, 2, 3}}, false, 10.0f);
    const auto* preserved = repo.findPath(711);
    REQUIRE(preserved != nullptr);
    REQUIRE(preserved->fromDBC);
    REQUIRE(preserved->spline.keyCount() == 2);
}

// ── Repository: hasUsableMovingPathForEntry ─────────────────────

TEST_CASE("hasUsableMovingPathForEntry rejects stationary/z-only", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Single-point path (stationary)
    std::vector<math::SplineKey> keys1 = {{0, {0, 0, 0}}};
    math::CatmullRomSpline sp1(std::move(keys1), false);
    repo.storePath(800, game::PathEntry(std::move(sp1), 800, false, true, false));
    REQUIRE(repo.hasUsableMovingPathForEntry(800) == false);

    // Z-only path (flagged)
    std::vector<math::SplineKey> keys2 = {{0, {0, 0, 0}}, {1000, {0, 0, 5}}};
    math::CatmullRomSpline sp2(std::move(keys2), false);
    repo.storePath(801, game::PathEntry(std::move(sp2), 801, true, true, false));
    REQUIRE(repo.hasUsableMovingPathForEntry(801) == false);

    // Moving XY path
    std::vector<math::SplineKey> keys3 = {{0, {0, 0, 0}}, {1000, {100, 0, 0}}};
    math::CatmullRomSpline sp3(std::move(keys3), false);
    repo.storePath(802, game::PathEntry(std::move(sp3), 802, false, true, false));
    REQUIRE(repo.hasUsableMovingPathForEntry(802) == true);
}

// ── Repository: inferDbcPathForSpawn ────────────────────────────

TEST_CASE("inferDbcPathForSpawn finds nearest DBC path", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Path A at (100, 0, 0)
    std::vector<math::SplineKey> keysA = {{0, {100, 0, 0}}, {1000, {200, 0, 0}}};
    math::CatmullRomSpline spA(std::move(keysA), false);
    repo.storePath(10, game::PathEntry(std::move(spA), 10, false, true, false));

    // Path B at (500, 0, 0)
    std::vector<math::SplineKey> keysB = {{0, {500, 0, 0}}, {1000, {600, 0, 0}}};
    math::CatmullRomSpline spB(std::move(keysB), false);
    repo.storePath(20, game::PathEntry(std::move(spB), 20, false, true, false));

    // Spawn near Path A
    uint32_t result = repo.inferDbcPathForSpawn({105.0f, 0.0f, 0.0f}, 200.0f, true);
    REQUIRE(result == 10);

    // Spawn near Path B
    result = repo.inferDbcPathForSpawn({510.0f, 0.0f, 0.0f}, 200.0f, true);
    REQUIRE(result == 20);

    // Spawn too far from both
    result = repo.inferDbcPathForSpawn({9999.0f, 0.0f, 0.0f}, 200.0f, true);
    REQUIRE(result == 0);
}

TEST_CASE("inferMovingPathForSpawn skips z-only paths", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Z-only path near spawn
    std::vector<math::SplineKey> keys = {{0, {10, 0, 0}}, {1000, {10, 0, 5}}};
    math::CatmullRomSpline sp(std::move(keys), false);
    repo.storePath(30, game::PathEntry(std::move(sp), 30, true, true, false));

    // inferMovingPathForSpawn passes allowZOnly=false
    uint32_t result = repo.inferMovingPathForSpawn({10.0f, 0.0f, 0.0f}, 200.0f);
    REQUIRE(result == 0);
}

TEST_CASE("night-elf ships never borrow unrelated TransportAnimation paths",
          "[transport_path_repo][transport][expansion]") {
    game::TransportPathRepository repo;
    std::vector<math::SplineKey> keys = {
        {0, {0, 0, 0}}, {1000, {100, 0, 0}}
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    repo.storePath(194675, game::PathEntry(std::move(spline), 194675, false, true, false));

    REQUIRE(repo.pickFallbackMovingPath(176244, 7087) == 0); // Moonspray
    REQUIRE(repo.pickFallbackMovingPath(181646, 7087) == 0); // Elune's Blessing
    REQUIRE(repo.pickFallbackMovingPath(177233, 7087) == 0); // shared model, other route
}

TEST_CASE("zeppelins never borrow another transport's TransportAnimation path",
          "[transport_path_repo][transport][expansion]") {
    game::TransportPathRepository repo;
    // 193182 and 193183 are real DBC paths - and they belong to two Cataclysm
    // transports. A table used to hand them to the five WotLK zeppelins, none
    // of which appears in TransportAnimation.dbc at all, so each flew a
    // stranger's loop while the server flew the real route.
    for (uint32_t pathId : {193182u, 193183u}) {
        std::vector<math::SplineKey> keys = {{0, {0, 0, 0}}, {1000, {100, 0, 0}}};
        math::CatmullRomSpline spline(std::move(keys), false);
        repo.storePath(pathId, game::PathEntry(std::move(spline), pathId, false, true, false));
    }

    constexpr uint32_t kHordeZeppelin = 7546;
    constexpr uint32_t kZeppelin = 3031;
    CHECK(repo.pickFallbackMovingPath(164871, kZeppelin) == 0);       // The Thundercaller
    CHECK(repo.pickFallbackMovingPath(175080, kZeppelin) == 0);       // The Iron Eagle
    CHECK(repo.pickFallbackMovingPath(176495, kZeppelin) == 0);       // The Purple Princess
    CHECK(repo.pickFallbackMovingPath(181689, kHordeZeppelin) == 0);  // Cloudkisser
    CHECK(repo.pickFallbackMovingPath(186238, kHordeZeppelin) == 0);  // The Mighty Wind
}

// ── Repository: taxi paths ─────────────────────────────────────

TEST_CASE("hasTaxiPath and findTaxiPath", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    REQUIRE(repo.hasTaxiPath(100) == false);
    REQUIRE(repo.findTaxiPath(100, 0) == nullptr);

    // loadTaxiPathNodeDBC would populate this, but we can't test it without AssetManager.
    // This just verifies the API works with empty data.
}

// ── Spline evaluation through PathEntry (Phase 3 integration) ──

TEST_CASE("PathEntry spline evaluates position at midpoint", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // At t=1000ms, should be near (100, 0, 0) - exactly at key 1
    glm::vec3 pos = entry.spline.evaluatePosition(1000);
    requireVec3Near(pos, 100.0f, 0.0f, 0.0f);
}

TEST_CASE("PathEntry spline evaluates position at interpolated time", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // At t=500ms, should be approximately (50, 0, 0)
    glm::vec3 pos = entry.spline.evaluatePosition(500);
    REQUIRE(pos.x > 40.0f);
    REQUIRE(pos.x < 60.0f);
    REQUIRE(std::abs(pos.y) < 1.0f);
}

TEST_CASE("PathEntry spline evaluate returns tangent for orientation", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // Tangent at midpoint should point roughly in +X direction
    auto result = entry.spline.evaluate(1000);
    REQUIRE(result.tangent.x > 0.0f);
    REQUIRE(std::abs(result.tangent.y) < 1.0f);
}

TEST_CASE("PathEntry findNearestKey finds closest waypoint", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // Point near key 1 (100, 0, 0)
    size_t nearest = entry.spline.findNearestKey({105.0f, 0.0f, 0.0f});
    REQUIRE(nearest == 1);

    // Point near key 2 (200, 0, 0)
    nearest = entry.spline.findNearestKey({195.0f, 0.0f, 0.0f});
    REQUIRE(nearest == 2);
}


// ── Taxi routes: one journey, one clock, a view per map ────────

/// Taxi path 737 verbatim from TaxiPathNode.dbc: the Undercity zeppelin.
/// Nodes 0-5 circle Vengeance Landing on map 571, nodes 6-16 circle the
/// Undercity tower on map 0, and the route closes from 16 back to 0. Both
/// joins are map changes - the server teleports the hull and the rider gets a
/// loading screen. The dock stops are 60s each.
static game::TaxiRoute zeppelinRoute737() {
    game::TaxiRoute route;
    route.nodes = {
        {{1933.1f, -6500.4f,  86.1f}, 571, 0u},
        {{2065.0f, -6331.5f,  86.1f}, 571, 0u},
        {{2059.0f, -6194.8f,  86.1f}, 571, 0u},
        {{1989.4f, -6082.8f,  85.6f}, 571, 60000u},
        {{1807.1f, -6001.6f,  86.2f}, 571, 0u},
        {{1671.7f, -6134.8f,  82.9f}, 571, 0u},
        {{2344.7f,   620.8f, 140.0f},   0, 0u},
        {{2306.9f,   554.1f, 134.1f},   0, 0u},
        {{2251.1f,   504.5f, 126.2f},   0, 0u},
        {{2149.1f,   429.6f, 102.6f},   0, 0u},
        {{2056.5f,   381.6f, 100.4f},   0, 60000u},
        {{2005.0f,   370.3f,  94.2f},   0, 0u},
        {{1978.1f,   410.0f, 100.5f},   0, 0u},
        {{2041.0f,   449.2f, 100.9f},   0, 0u},
        {{2091.8f,   481.2f, 109.9f},   0, 0u},
        {{2173.6f,   534.1f, 128.9f},   0, 0u},
        {{2235.2f,   575.6f, 141.8f},   0, 0u},
    };
    route.circuit = game::TransportPathRepository::taxiRouteIsCircuit(route);
    return route;
}

TEST_CASE("a route that ends on another map is a ring", "[transport_path_repo][taxi]") {
    const auto route = zeppelinRoute737();
    REQUIRE(route.circuit);

    // A harbour shuttle on one map, ends the whole run apart, is not.
    game::TaxiRoute shuttle;
    shuttle.nodes = {
        {{0.0f, 0.0f, 0.0f}, 0, 0u},
        {{500.0f, 0.0f, 0.0f}, 0, 60000u},
        {{1000.0f, 0.0f, 0.0f}, 0, 0u},
    };
    REQUIRE_FALSE(game::TransportPathRepository::taxiRouteIsCircuit(shuttle));
}

TEST_CASE("every map's slice of a route shares one cycle", "[transport_path_repo][taxi]") {
    const auto route = zeppelinRoute737();
    const auto onDurotar = game::TransportPathRepository::buildTaxiRouteSlice(route, 0);
    const auto onFjord = game::TransportPathRepository::buildTaxiRouteSlice(route, 571);

    // The server publishes one phase against one route, so a fraction of the
    // cycle has to mean the same thing on either map.
    REQUIRE(onDurotar.spline.durationMs() == onFjord.spline.durationMs());
    REQUIRE(onDurotar.spline.durationMs() > 120000u);   // both 60s dock stops
}

TEST_CASE("a slice says when the hull is somewhere else", "[transport_path_repo][taxi]") {
    const auto route = zeppelinRoute737();
    const auto onDurotar = game::TransportPathRepository::buildTaxiRouteSlice(route, 0);
    const auto onFjord = game::TransportPathRepository::buildTaxiRouteSlice(route, 571);
    const uint32_t cycle = onDurotar.spline.durationMs();

    REQUIRE_FALSE(onDurotar.presentMs.empty());
    REQUIRE_FALSE(onFjord.presentMs.empty());

    // The hull is on exactly one of them at any moment, and on each for some
    // of the cycle. Neither map gets to animate it the whole way round.
    uint32_t here = 0, there = 0, both = 0, neither = 0;
    for (uint32_t t = 0; t < cycle; t += cycle / 200) {
        const bool a = onDurotar.presentAt(t);
        const bool b = onFjord.presentAt(t);
        if (a && b) both++;
        else if (a) here++;
        else if (b) there++;
        else neither++;
    }
    CHECK(both == 0);
    CHECK(neither == 0);
    CHECK(here > 10);
    CHECK(there > 10);

    // Fjord first, since the route's nodes start there.
    CHECK(onFjord.presentAt(0));
    CHECK_FALSE(onDurotar.presentAt(0));
}

TEST_CASE("a hull holds still on the map it has left", "[transport_path_repo][taxi]") {
    const auto route = zeppelinRoute737();
    const auto onDurotar = game::TransportPathRepository::buildTaxiRouteSlice(route, 0);
    const uint32_t cycle = onDurotar.spline.durationMs();

    // Interpolating across the gap would sweep the hull over Tirisfal in one
    // frame. While it is away the slice does not move at all.
    glm::vec3 prev = onDurotar.spline.evaluatePosition(0);
    float worst = 0.0f;
    for (uint32_t t = 0; t < cycle; t += 250) {
        if (onDurotar.presentAt(t)) { prev = onDurotar.spline.evaluatePosition(t); continue; }
        const glm::vec3 cur = onDurotar.spline.evaluatePosition(t);
        worst = std::max(worst, glm::distance(cur, prev));
        prev = cur;
    }
    CHECK(worst < 1.0f);
}

TEST_CASE("a zeppelin flies its tower circuit round, not backwards",
          "[transport_path_repo][taxi][transport]") {
    // The slice used to be flown out and then retraced node by node, so the
    // hull departed the tower and came straight back to arrive at it again -
    // which is what a rider reported seeing on both sides of the trip.
    const auto route = zeppelinRoute737();
    const auto onDurotar = game::TransportPathRepository::buildTaxiRouteSlice(route, 0);

    // Each of the eleven map-0 nodes is passed once per cycle. Retracing would
    // pass nine of them twice.
    int passes = 0;
    const uint32_t cycle = onDurotar.spline.durationMs();
    bool near = false;
    for (uint32_t t = 0; t <= cycle; t += 100) {
        if (!onDurotar.presentAt(t)) { near = false; continue; }
        const bool nowNear =
            glm::distance(onDurotar.spline.evaluatePosition(t),
                          glm::vec3(2149.1f, 429.6f, 102.6f)) < 12.0f;
        if (nowNear && !near) passes++;
        near = nowNear;
    }
    CHECK(passes == 1);
}

TEST_CASE("a single-map route is always present and closes its ring",
          "[transport_path_repo][taxi]") {
    // The Grom'gol zeppelin's path 301 never leaves map 0: an open 18km run
    // that the hull flies out and back along. It is here the whole cycle.
    game::TaxiRoute shuttle;
    shuttle.nodes = {
        {{0.0f, 0.0f, 0.0f}, 0, 0u},
        {{500.0f, 0.0f, 0.0f}, 0, 60000u},
        {{1000.0f, 0.0f, 0.0f}, 0, 0u},
    };
    shuttle.circuit = game::TransportPathRepository::taxiRouteIsCircuit(shuttle);
    const auto slice = game::TransportPathRepository::buildTaxiRouteSlice(shuttle, 0);

    CHECK(slice.presentMs.empty());          // always here
    CHECK(slice.presentAt(0));
    CHECK(slice.presentAt(slice.spline.durationMs() / 2));

    // Position-closed, so the phase wrap is seamless.
    const glm::vec3 atStart = slice.spline.evaluatePosition(0);
    const glm::vec3 atEnd = slice.spline.evaluatePosition(slice.spline.durationMs());
    CHECK(glm::distance(atStart, atEnd) < 0.5f);
    // And it actually leaves the dock.
    CHECK(slice.spline.evaluatePosition(slice.spline.durationMs() / 2).x > 400.0f);
}

TEST_CASE("the hull's speed is solved from the period the server publishes",
          "[transport_path_repo][taxi]") {
    auto route = zeppelinRoute737();
    route.cycleDwellMs = 120000u;
    route.cycleDistance = 0.0f;
    for (size_t i = 0; i < route.nodes.size(); ++i) {
        const auto& a = route.nodes[i];
        const auto& b = route.nodes[(i + 1) % route.nodes.size()];
        if (a.mapId == b.mapId) route.cycleDistance += glm::distance(a.position, b.position);
    }

    // The dwells are fixed and the legs are not, so matching the period is not
    // the same as scaling the cycle: the dock stop has to land in the right
    // place within it.
    const float speed = game::TransportPathRepository::taxiRouteSpeedFor(route, 203000u);
    REQUIRE(speed > 1.0f);
    const auto slice = game::TransportPathRepository::buildTaxiRouteSlice(route, 0, speed);
    CHECK(slice.spline.durationMs() == Catch::Approx(203000u).margin(2000));

    // A period that cannot describe this route is refused rather than believed.
    CHECK(game::TransportPathRepository::taxiRouteSpeedFor(route, 0u) == 0.0f);
    CHECK(game::TransportPathRepository::taxiRouteSpeedFor(route, 119000u) == 0.0f);
}
