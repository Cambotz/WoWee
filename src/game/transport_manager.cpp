#include "game/transport_manager.hpp"

#include <set>
#include "game/transport_clock_sync.hpp"
#include "game/transport_animator.hpp"
#include "game/game_utils.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/m2_renderer.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <chrono>
#include <cmath>
#include <limits>

namespace wowee::game {

namespace {

bool seedDeeprunTramStationPhase(ActiveTransport& transport,
                                 const PathEntry& pathEntry,
                                 const glm::vec3& serverPosition) {
    const auto& spline = pathEntry.spline;
    const auto& keys = spline.keys();
    if (!pathEntry.fromDBC || spline.durationMs() == 0 || keys.empty()) {
        return false;
    }

    size_t maxOffsetIdx = 0;
    float maxAbsX = 0.0f;
    for (size_t i = 0; i < keys.size(); ++i) {
        const float absX = std::abs(keys[i].position.x);
        if (absX > maxAbsX) {
            maxAbsX = absX;
            maxOffsetIdx = i;
        }
    }

    if (maxAbsX < 100.0f) {
        return false;
    }

    const bool pathMaxIsPositive = keys[maxOffsetIdx].position.x > 0.0f;
    const bool serverAtPositiveEnd = serverPosition.x > 1000.0f;
    const bool useMaxOffsetStation = (pathMaxIsPositive == serverAtPositiveEnd);

    size_t stationIdx = maxOffsetIdx;
    if (!useMaxOffsetStation) {
        stationIdx = 0;
        for (size_t step = 1; step < keys.size(); ++step) {
            const size_t idx = (maxOffsetIdx + step) % keys.size();
            if (std::abs(keys[idx].position.x) < 1.0f) {
                stationIdx = idx;
                break;
            }
        }
    }

    // CMaNGOS's "presence echo" for these objects is always the same static DB spawn
    // row, not a live position - it tells us this spawn coordinate corresponds to
    // local path point stationIdx (a geometric fact about how this entry's path is
    // anchored in world space), not that the tram is *currently* there. Correct
    // basePosition so the path curve sits in the right place; leave localClockMs
    // alone so it keeps reflecting the absolute-time-derived phase set at
    // registration (see nowEpochMs comment above) instead of snapping back to
    // "docked at this waypoint" every time an echo arrives.
    const glm::vec3 stationOffset = keys[stationIdx].position;
    const glm::vec3 candidateBasePosition = serverPosition - stationOffset;

    // The useMaxOffsetStation==true branch assumes the server's static echo represents
    // the tram docked at the path's far/max-offset end - true for most of these six
    // paths, but at least one (176085) has local key data where that assumption is
    // simply wrong: the echo position is really the near-origin "home" point, and
    // trusting the heuristic silently shifted basePosition by ~2481 units toward the
    // opposite station. That desyncs this car's *logical* position (used for boarding
    // distance and path evaluation) from where it's actually spawned/rendered, while
    // leaving it looking fine on screen - "climbed onto the middle car, it drove away
    // without me, hung in midair" reported live, and very likely also this entry's
    // contribution to "solo cars" (its real position no longer lines up with its train).
    // registerTransport() already computed a sane basePosition from the spawn position
    // directly; a legitimate correction should only be a modest nudge from that, not a
    // jump on the order of the whole route length. Reject implausible corrections
    // rather than trust the heuristic blindly.
    // Note: returning true here (not false) is deliberate even though no correction is
    // applied. The caller's false-branch fallback resets localClockMs to 0 and slams
    // basePosition to the raw server position - correct for the genuinely-no-usable-path
    // cases above, but here registerTransport() already left the transport in a good
    // state (sane basePosition, correctly-seeded localClockMs); rejecting a bad
    // correction should mean "leave that alone", not "wipe it and start over".
    constexpr float kMaxPlausibleCorrectionDist = 300.0f;
    if (glm::distance(candidateBasePosition, transport.basePosition) > kMaxPlausibleCorrectionDist) {
        // Entry 176085 rejects this every re-sync cycle by design (its real spawn is
        // legitimately far from the heuristic's candidate) - routine, not worth WARN.
        LOG_DEBUG("Deeprun tram station anchor correction rejected as implausible: guid=0x",
                    std::hex, transport.guid, std::dec,
                    " entry=", transport.entry, " pathId=", transport.pathId,
                    " candidateBase=(", candidateBasePosition.x, ",", candidateBasePosition.y, ",", candidateBasePosition.z, ")",
                    " existingBase=(", transport.basePosition.x, ",", transport.basePosition.y, ",", transport.basePosition.z, ")");
        return true;
    }

    transport.basePosition = candidateBasePosition;
    transport.position = transport.basePosition + spline.evaluatePosition(transport.localClockMs % spline.durationMs());
    transport.hasServerYaw = false;

    LOG_DEBUG("Deeprun tram station anchor corrected: guid=0x", std::hex, transport.guid, std::dec,
                " entry=", transport.entry,
                " pathId=", transport.pathId,
                " localClockMs=", transport.localClockMs,
                " stationOffset=(", stationOffset.x, ",", stationOffset.y, ",", stationOffset.z, ")",
                " base=(", transport.basePosition.x, ",", transport.basePosition.y, ",", transport.basePosition.z, ")",
                " serverPos=(", serverPosition.x, ",", serverPosition.y, ",", serverPosition.z, ")");
    return true;
}

} // namespace

TransportManager::TransportManager() = default;
TransportManager::~TransportManager() = default;

void TransportManager::update(float deltaTime) {
    elapsedTime_ += deltaTime;

    for (auto& [guid, transport] : transports_) {
        // Once we have server clock offset, we can predict server time indefinitely
        // No need for watchdog - keep using the offset even if server updates stop
        updateTransportMovement(transport, deltaTime);
    }
}

void TransportManager::registerTransport(uint64_t guid,
                                         uint32_t wmoInstanceId,
                                         uint32_t pathId,
                                         const glm::vec3& spawnWorldPos,
                                         uint32_t entry,
                                         uint32_t displayId,
                                         bool isM2,
                                         float spawnOrientation) {
    auto* pathEntry = pathRepo_.findPath(pathId);
    if (!pathEntry) {
        LOG_ERROR("TransportManager: Path ", pathId, " not found for transport ", guid);
        return;
    }

    const auto& spline = pathEntry->spline;
    if (spline.keyCount() == 0) {
        LOG_ERROR("TransportManager: Path ", pathId, " has no waypoints");
        return;
    }

    ActiveTransport transport;
    transport.guid = guid;
    transport.wmoInstanceId = wmoInstanceId;
    transport.pathId = pathId;
    transport.entry = entry;
    transport.displayId = displayId;
    transport.isM2 = isM2;
    transport.worldCoords = pathEntry->worldCoords;
    transport.allowBootstrapVelocity = false;

    // A moving path registered under someone else's entry is a borrowed route.
    //
    // Marked here rather than where the borrowing is decided, because it is
    // decided in more than one place: the Tirisfal zeppelin was flagged on its
    // first spawn and then respawned unflagged on the far side of a world
    // transfer, so the arrival-side instance went back to flying the wrong
    // route with a passenger aboard. Every registration comes through here.
    if (!isM2 && !pathEntry->worldCoords && spline.durationMs() > 0 &&
        spline.keyCount() > 1 && pathId != entry) {
        transport.borrowedPath = true;
        static std::set<uint32_t> saidBorrow;
        if (saidBorrow.insert(entry).second) {
            LOG_WARNING("Transport entry=", entry, " is flying path ", pathId,
                        ", which is not its own - following server position "
                        "updates instead of animating it");
        }
    }

    // CRITICAL: Set basePosition from spawn position and t=0 offset
    // For stationary paths (1 waypoint), just use spawn position directly
    if (spline.durationMs() == 0 || spline.keyCount() <= 1 || transport.borrowedPath) {
        // Stationary transport, or one on a borrowed route it will not fly -
        // either way the spawn position is where it is, not an offset into a
        // path. Inferring a base from the borrowed spline's first waypoint put
        // the hull a whole route away from the dock the server spawned it at.
        transport.basePosition = spawnWorldPos;
        transport.position = spawnWorldPos;
    } else if (pathEntry->worldCoords) {
        // World-coordinate path (TaxiPathNode) - points are absolute world positions
        transport.basePosition = glm::vec3(0.0f);
        transport.position = spline.evaluatePosition(0);
    } else {
        // Moving transport - infer base from first path offset
        glm::vec3 offset0 = spline.evaluatePosition(0);
        transport.basePosition = spawnWorldPos - offset0;  // Infer base from spawn
        transport.position = spawnWorldPos;  // Start at spawn position (base + offset0)

        // TransportAnimation paths are local offsets; first waypoint is expected near origin.
        // Warn only if the local path itself looks suspicious.
        glm::vec3 firstWaypoint = spline.keys()[0].position;
        if (glm::dot(firstWaypoint, firstWaypoint) > 100.0f) {
            LOG_WARNING("Transport 0x", std::hex, guid, std::dec, " path ", pathId,
                        ": first local waypoint far from origin: (",
                        firstWaypoint.x, ",", firstWaypoint.y, ",", firstWaypoint.z, ")");
        }
    }

    // The authored yaw, carrying the same offset the spawner applies for this
    // model kind: an M2's default facing is +renderX and every M2 game object
    // is turned 90° for it (renderYawM2go), while a WMO is placed unturned.
    // The transform below is rebuilt from scratch every frame, so a rotation
    // that skips the offset puts the model at right angles to where it was
    // placed. Only route-less transports read this - a tangent-derived heading
    // carries its own convention and is left alone.
    transport.spawnYaw = spawnOrientation +
        (isM2 ? glm::half_pi<float>() : 0.0f);
    // Identity discarded the authored placement, so every transport started
    // life facing the same way whatever the server said.
    transport.rotation = glm::angleAxis(transport.spawnYaw, glm::vec3(0.0f, 0.0f, 1.0f));
    transport.playerOnBoard = false;
    transport.playerLocalOffset = glm::vec3(0.0f);
    transport.hasDeckBounds = false;
    transport.localClockMs = 0;
    transport.hasServerClock = false;
    transport.serverClockOffsetMs = 0;
    // Start with client-side animation for all DBC paths with real movement.
    // If the server sends actual position updates, updateServerTransport() will switch
    // to server-driven mode. This ensures transports like trams (which the server doesn't
    // stream updates for) still animate, while ships/zeppelins switch to server authority.
    // The borrowed-path test above turned this off and this line turned it back
    // on, sixty lines later and out of sight: the zeppelin flew the borrowed
    // route under client animation while the server drove it somewhere else, so
    // it played both journeys at once and read as two hulls.
    transport.useClientAnimation =
        (pathEntry->fromDBC && spline.durationMs() > 0 && !transport.borrowedPath);
    transport.clientAnimationReverse = false;
    transport.serverYaw = 0.0f;
    transport.hasServerYaw = false;
    transport.dockYaw = 0.0f;
    transport.hasDockYaw = false;
    transport.serverYawFlipped180 = false;
    transport.serverYawAlignmentScore = 0;
    transport.lastServerUpdate = 0.0f;
    transport.serverUpdateCount = 0;
    transport.serverLinearVelocity = glm::vec3(0.0f);
    transport.serverAngularVelocity = 0.0f;
    transport.hasServerVelocity = false;

    if (transport.useClientAnimation && spline.durationMs() > 0) {
        // Seed to a stable phase derived from absolute time (see nowEpochMs comment)
        // so elevators don't all start at t=0, and so paired/opposing transports like
        // the Deeprun Tram's two cars land at the same relative phase for every client.
        // Deeprun tram cars use a rounded duration for the modulo (see
        // deeprunTramSeedDurationMs comment) since sibling cars' own path durations
        // don't quite match and that mismatch was decorrelating their seeds.
        const uint32_t seedDurationMs = isDeeprunTramTransport(transport)
            ? deeprunTramSeedDurationMs(spline.durationMs())
            : spline.durationMs();
        transport.localClockMs = static_cast<uint32_t>(nowEpochMs() % seedDurationMs);
        LOG_INFO("TransportManager: Enabled client animation for transport 0x",
                 std::hex, guid, std::dec, " path=", pathId,
                 " durationMs=", spline.durationMs(), " seedMs=", transport.localClockMs,
                 (pathEntry->worldCoords ? " [worldCoords]" : (pathEntry->zOnly ? " [z-only]" : "")));
    }

    updateTransformMatrices(transport);

    // CRITICAL: Update WMO renderer with initial transform
    pushTransform(transport);

    transports_[guid] = transport;

    // A route phase the server published before this was registered.
    if (auto pendingClock = pendingRouteClocks_.find(guid);
        pendingClock != pendingRouteClocks_.end()) {
        const auto [phase, periodMs] = pendingClock->second;
        pendingRouteClocks_.erase(pendingClock);
        applyServerRouteClock(guid, phase, periodMs);
    }

    glm::vec3 renderPos = core::coords::canonicalToRender(transport.position);
    LOG_INFO("TransportManager: Registered transport 0x", std::hex, guid, std::dec,
             " at path ", pathId, " with ", (pathEntry ? pathEntry->spline.keyCount() : 0u), " waypoints",
             " wmoInstanceId=", wmoInstanceId,
             " entry=", entry,
             " displayId=", displayId,
             " isM2=", isM2,
             " spawnPos=(", spawnWorldPos.x, ", ", spawnWorldPos.y, ", ", spawnWorldPos.z, ")",
             " basePos=(", transport.basePosition.x, ", ", transport.basePosition.y, ", ", transport.basePosition.z, ")",
             " initialRenderPos=(", renderPos.x, ", ", renderPos.y, ", ", renderPos.z, ")");

    if (isDeeprunTramTransport(transport)) {
        LOG_DEBUG("Deeprun tram registered: guid=0x", std::hex, guid, std::dec,
                    " entry=", entry,
                    " displayId=", displayId,
                    " pathId=", pathId,
                    " instanceId=", wmoInstanceId,
                    " isM2=", isM2,
                    " mode=", (transport.useClientAnimation ? "client" : "server"),
                    " spawn=(", spawnWorldPos.x, ",", spawnWorldPos.y, ",", spawnWorldPos.z, ")",
                    " base=(", transport.basePosition.x, ",", transport.basePosition.y, ",", transport.basePosition.z, ")");
    }
}
void TransportManager::clearTransports() {
    const size_t count = transports_.size();
    transports_.clear();
    // Held phases belong to the map that published them; a reused GUID on the
    // next map must not inherit one.
    pendingRouteClocks_.clear();
    if (count != 0) {
        LOG_INFO("TransportManager: Cleared ", count, " transports for map transition");
    }
}

void TransportManager::resolveAndRegisterSpawn(uint64_t guid,
                                               uint32_t entry,
                                               uint32_t displayId,
                                               const glm::vec3& canonicalSpawnPos,
                                               uint32_t wmoInstanceId,
                                               bool isM2,
                                               bool preferServerData,
                                               float spawnOrientation) {
    // TransportAnimation.dbc is indexed by GameObject entry.
    uint32_t pathId = entry;

    // Check if we have a real usable path, otherwise remap/infer/fall back to stationary.
    // Elevators used to be in this list - 807, 808, 2454 and 1587 are lifts,
    // not airships - and the stricter "must travel 25 units" check below then
    // rejected their short vertical path, dropping them into the inference
    // that borrows a nearby route.
    const bool shipOrZeppelinDisplay = isVehicleTransportDisplay(displayId);
    bool hasUsablePath = hasPathForEntry(entry);
    if (shipOrZeppelinDisplay) {
        hasUsablePath = hasUsableMovingPathForEntry(entry, 25.0f);
    }
    if (preferServerData) {
        // Strict server-authoritative mode: no inferred/remapped fallback routes.
        if (!hasUsablePath) {
            std::vector<glm::vec3> path = { canonicalSpawnPos };
            loadPathFromNodes(pathId, path, false, 0.0f);
            LOG_INFO("Auto-spawned transport in strict server-first mode (stationary fallback): entry=", entry,
                     " displayId=", displayId, " wmoInstance=", wmoInstanceId);
        } else {
            LOG_INFO("Auto-spawned transport in server-first mode with entry DBC path: entry=", entry,
                     " displayId=", displayId, " wmoInstance=", wmoInstanceId);
        }
    } else if (!hasUsablePath) {
        bool allowZOnly = (displayId == 455 || displayId == 462);
        // Continent-crossing ships are server-driven MO_TRANSPORT objects whose
        // route only ever comes from their taxi path (TaxiPathNode.dbc via GO
        // template data[0]), assigned by the GO-query hook. They must NOT infer a
        // nearby TransportAnimation.dbc path at spawn: a ship spawned in a harbour
        // can otherwise borrow an unrelated local animation loop (e.g. an elevator)
        // and circle in place until - or unless - its taxi path arrives. The ship
        // guard in pickFallbackMovingPath already returns 0 for these displays; skip
        // inference too so the same guard actually holds, leaving the ship docked.
        const bool looksLikeShip = isOceanGoingTransportDisplay(displayId);
        uint32_t inferredPath =
            looksLikeShip ? 0u : inferDbcPathForSpawn(canonicalSpawnPos, 1200.0f, allowZOnly);
        if (inferredPath != 0) {
            pathId = inferredPath;
            LOG_INFO("Auto-spawned transport with inferred path: entry=", entry,
                     " inferredPath=", pathId, " displayId=", displayId,
                     " wmoInstance=", wmoInstanceId);
        } else {
            uint32_t remappedPath = pickFallbackMovingPath(entry, displayId);
            if (remappedPath != 0) {
                pathId = remappedPath;
                LOG_INFO("Auto-spawned transport with remapped fallback path: entry=", entry,
                         " remappedPath=", pathId, " displayId=", displayId,
                         " wmoInstance=", wmoInstanceId);
            } else {
                std::vector<glm::vec3> path = { canonicalSpawnPos };
                loadPathFromNodes(pathId, path, false, 0.0f);
                LOG_INFO("Auto-spawned transport with stationary path: entry=", entry,
                         " displayId=", displayId, " wmoInstance=", wmoInstanceId);
            }
        }
    } else {
        LOG_INFO("Auto-spawned transport with real path: entry=", entry,
                 " displayId=", displayId, " wmoInstance=", wmoInstanceId);
    }

    registerTransport(guid, wmoInstanceId, pathId, canonicalSpawnPos, entry, displayId,
                      isM2, spawnOrientation);

    if (displayId == 3831u) {
        if (auto* tr = getTransport(guid)) {
            LOG_DEBUG("Auto-spawned Deeprun tram transport: guid=0x",
                        std::hex, guid, std::dec,
                        " entry=", entry,
                        " pathId=", tr->pathId,
                        " isM2=", tr->isM2,
                        " mode=", (tr->useClientAnimation ? "client" : "server"));
        }
    }
}

ActiveTransport* TransportManager::getTransport(uint64_t guid) {
    auto it = transports_.find(guid);
    if (it != transports_.end()) {
        return &it->second;
    }
    return nullptr;
}

glm::vec3 TransportManager::getPlayerWorldPosition(uint64_t transportGuid, const glm::vec3& localOffset) {
    auto* transport = getTransport(transportGuid);
    if (!transport) {
        LOG_WARNING("getPlayerWorldPosition: transport 0x", std::hex, transportGuid, std::dec,
                    " not found - returning localOffset as-is (callers should guard)");
        return localOffset;
    }

    if (transport->isM2) {
        // M2 transports (trams): localOffset is a canonical world-space delta
        // from the transport's canonical position. Just add directly.
        return transport->position + localOffset;
    }

    // WMO transports (ships): localOffset is in transport-local space,
    // use the render-space transform matrix.
    glm::vec4 localPos(localOffset, 1.0f);
    glm::vec4 worldPos = transport->transform * localPos;
    // transform is a renderer-space matrix; every caller of this API expects
    // canonical world coordinates. Returning it raw swaps X/Y again when the
    // application converts canonical -> render, leaving riders behind or mirrored.
    return core::coords::renderToCanonical(glm::vec3(worldPos));
}

glm::vec3 TransportManager::serverToTransportLocal(
    uint64_t transportGuid, const glm::vec3& serverOffset) const {
    const auto it = transports_.find(transportGuid);
    // WMO transport movement blocks already express offsets in the model's
    // local axes. Their render transform consumes those axes directly; applying
    // the world-space server->canonical X/Y swap here displaced deck NPCs and
    // server-attached players across the hull. M2 transports retain their older
    // canonical-delta representation.
    if (it != transports_.end() && !it->second.isM2) return serverOffset;
    return core::coords::serverToCanonical(serverOffset);
}

bool TransportManager::isPointOnTransportDeck(uint64_t transportGuid,
                                               const glm::vec3& canonicalPosition,
                                               float maxFloorDelta) const {
    const auto floor = getTransportDeckFloorHeight(transportGuid, canonicalPosition);
    if (!floor) return false;

    const float floorDelta = canonicalPosition.z - *floor;
    return floorDelta >= -0.35f && floorDelta <= maxFloorDelta;
}

void TransportManager::applyServerRouteClock(uint64_t transportGuid, float phase,
                                             uint32_t periodMs) {
    if (periodMs == 0) return;
    if (!(phase >= 0.0f) || phase >= 1.0f) return;   // also rejects NaN

    auto* transport = getTransport(transportGuid);
    if (!transport) {
        // The create block carries this, and a transport is not registered
        // when it arrives - registration waits on the model loading, which
        // takes several frames. The server publishes the phase when the object
        // is created and rarely again, so dropping it here meant a hull that
        // never adopted the server's clock at all and invented one from
        // distance over speed instead. Held until registerTransport asks.
        pendingRouteClocks_[transportGuid] = {phase, periodMs};
        return;
    }

    const bool firstSample = !transport->hasServerRouteClock;
    transport->hasServerRouteClock = true;
    transport->routePhase = phase;
    transport->routePeriodMs = periodMs;
    transport->routePhaseAtTime = elapsedTime_;

    if (firstSample) {
        LOG_WARNING("Transport 0x", std::hex, transportGuid, std::dec,
                 " adopted server route clock: period=", periodMs, "ms phase=", phase,
                 " (client period was ", [&]() -> uint32_t {
                     const auto* p = pathRepo_.findPath(transport->pathId);
                     return p ? p->spline.durationMs() : 0u;
                 }(), "ms)");
    }
}

void TransportManager::applyDoodadMotionState(ActiveTransport& transport, bool moving) {
    if (transport.isM2 || !wmoRenderer_ || transport.wmoInstanceId == 0) return;

    // 163 = ShipMoving, 164 = ShipStop. Stopping is a one-shot that settles into
    // the model's idle pose; running is a loop.
    constexpr uint32_t kShipMoving = 163u;
    constexpr uint32_t kShipStop = 164u;
    const uint32_t want = moving ? kShipMoving : kShipStop;

    const bool sameState = (transport.appliedDoodadAnim == static_cast<int>(want));
    if (sameState && transport.appliedDoodadCount != 0) return;

    const size_t touched =
        wmoRenderer_->setInstanceDoodadAnimation(transport.wmoInstanceId, want, moving);
    transport.appliedDoodadAnim = static_cast<int>(want);
    transport.appliedDoodadCount = touched;
}

bool TransportManager::isTransportCollisionReady(uint64_t transportGuid) const {
    if (!wmoRenderer_) return false;
    const auto it = transports_.find(transportGuid);
    if (it == transports_.end() || it->second.isM2 || it->second.wmoInstanceId == 0) {
        return false;
    }
    return wmoRenderer_->instanceHasCollisionGeometry(it->second.wmoInstanceId);
}

std::optional<bool> TransportManager::isPointOverM2Footprint(
    uint64_t transportGuid, const glm::vec3& canonicalPosition) const {
    if (!m2Renderer_) return std::nullopt;
    const auto it = transports_.find(transportGuid);
    if (it == transports_.end() || !it->second.isM2 || it->second.wmoInstanceId == 0) {
        return std::nullopt;
    }
    glm::vec3 boundsMin, boundsMax;
    if (!m2Renderer_->getInstanceWorldBounds(it->second.wmoInstanceId, boundsMin, boundsMax)) {
        return std::nullopt;
    }
    // The bounds are the renderer's, so the point has to be too.
    const glm::vec3 render = core::coords::canonicalToRender(canonicalPosition);
    // A step of slack, so stepping aboard from the edge still counts and
    // standing a car's width away does not.
    constexpr float kEdge = 0.6f;
    if (render.x < boundsMin.x - kEdge || render.x > boundsMax.x + kEdge ||
        render.y < boundsMin.y - kEdge || render.y > boundsMax.y + kEdge) {
        return false;
    }
    // And underfoot rather than a storey away: the car is as tall as it is
    // wide, and its origin can sit either end of that.
    return render.z >= boundsMin.z - 1.0f && render.z <= boundsMax.z + 1.0f;
}

std::optional<float> TransportManager::getTransportDeckFloorHeight(
    uint64_t transportGuid,
    const glm::vec3& canonicalPosition) const {
    if (!wmoRenderer_) return std::nullopt;
    const auto it = transports_.find(transportGuid);
    if (it == transports_.end() || it->second.isM2 || it->second.wmoInstanceId == 0) {
        return std::nullopt;
    }

    const glm::vec3 renderPosition = core::coords::canonicalToRender(canonicalPosition);
    float normalZ = 0.0f;
    const auto floor = wmoRenderer_->getInstanceFloorHeight(
        it->second.wmoInstanceId,
        renderPosition.x, renderPosition.y, renderPosition.z + 0.35f,
        &normalZ);
    if (!floor || normalZ < 0.55f) return std::nullopt;
    return floor;
}
void TransportManager::loadPathFromNodes(uint32_t pathId, const std::vector<glm::vec3>& waypoints, bool looping, float speed) {
    pathRepo_.loadPathFromNodes(pathId, waypoints, looping, speed);
}

void TransportManager::setDeckBounds(uint64_t guid, const glm::vec3& min, const glm::vec3& max) {
    auto* transport = getTransport(guid);
    if (!transport) {
        LOG_ERROR("TransportManager: Cannot set deck bounds for unknown transport ", guid);
        return;
    }

    transport->deckMin = min;
    transport->deckMax = max;
    transport->hasDeckBounds = true;
}

void TransportManager::updateTransportMovement(ActiveTransport& transport, float deltaTime) {
    auto* pathEntry = pathRepo_.findPath(transport.pathId);
    if (!pathEntry) {
        return;
    }

    // A borrowed route belongs to another transport. Its position comes from
    // updateServerTransport and nowhere else; evaluating the spline here is
    // what carried a rider along a journey the hull was not making.
    if (transport.borrowedPath) {
        updateTransformMatrices(transport);
        pushTransform(transport);
        applyDoodadMotionState(transport, /*moving=*/transport.hasServerVelocity);
        return;
    }

    const auto& spline = pathEntry->spline;
    if (spline.keyCount() == 0) {
        return;
    }

    // Stationary transport (durationMs = 0)
    if (spline.durationMs() == 0) {
        // Just update transform (position already set)
        updateTransformMatrices(transport);
        pushTransform(transport);
        applyDoodadMotionState(transport, /*moving=*/false);
        return;
    }

    // Compute path time via ClockSync
    uint32_t pathTimeMs = 0;
    if (!clockSync_.computePathTime(transport, spline, elapsedTime_, deltaTime, pathTimeMs)) {
        // Strict server-authoritative mode: do not guess movement between server snapshots.
        updateTransformMatrices(transport);
        pushTransform(transport);
        return;
    }

    // A cross-continent route spends part of its cycle on the other map. The
    // slice holds the hull still there rather than sweeping it across, and
    // there is nothing here to draw, stand on, or board until it returns.
    //
    // Not while someone is standing on it. Hiding the hull and skipping the
    // pose below takes the deck out from under a rider and freezes them at a
    // position the world has already streamed away from - the client has no
    // authority to carry them across a map boundary, only the server does, and
    // until it says so the honest thing is to keep the deck where it is. The
    // slice holds the hull at its last dock through the off-map stretch, so
    // this parks the rider at the pier rather than dropping them mid-ocean.
    const bool carryingRider = riderTransportGuid_ != 0 && transport.guid == riderTransportGuid_;
    const bool presentNow = pathEntry->presentAt(pathTimeMs) || carryingRider;
    if (presentNow != transport.onThisMap) {
        transport.onThisMap = presentNow;
        LOG_INFO("Transport 0x", std::hex, transport.guid, std::dec,
                 " entry=", transport.entry,
                 (presentNow ? " arrived on this map" : " left for the other map"),
                 " at route time ", pathTimeMs, "ms");
    }
    if (!presentNow) {
        setInstanceHidden(transport, true);
        applyDoodadMotionState(transport, /*moving=*/false);
        return;
    }

    // Evaluate position + rotation via Animator
    animator_.evaluateAndApply(transport, *pathEntry, pathTimeMs);

    // Update transform matrices
    updateTransformMatrices(transport);
    pushTransform(transport);
    applyDoodadMotionState(transport, /*moving=*/!transport.atDockDwell);

    // Debug logging every 600 frames (~10 seconds at 60fps)
    static int debugFrameCount = 0;
    if (debugFrameCount++ % 600 == 0) {
        LOG_DEBUG("Transport 0x", std::hex, transport.guid, std::dec,
                 " pathTime=", pathTimeMs, "ms / ", spline.durationMs(), "ms",
                 " pos=(", transport.position.x, ", ", transport.position.y, ", ", transport.position.z, ")",
                 " mode=", (transport.useClientAnimation ? "client" : "server"),
                 " isM2=", transport.isM2);
    }
}

// Push transform to the appropriate renderer (WMO or M2).
void TransportManager::setInstanceHidden(const ActiveTransport& transport, bool hidden) {
    // Only WMO hulls. An M2 transport is a tram car or a lift, which never
    // leaves its map, so onThisMap is always true for one and this is never
    // asked to hide it.
    if (!transport.isM2 && wmoRenderer_) {
        wmoRenderer_->setInstanceHidden(transport.wmoInstanceId, hidden);
    }
}

void TransportManager::pushTransform(ActiveTransport& transport) {
    setInstanceHidden(transport, !transport.onThisMap);
    if (transport.isM2) {
        if (m2Renderer_) m2Renderer_->setInstanceTransform(transport.wmoInstanceId, transport.transform);
    } else {
        if (wmoRenderer_) {
            wmoRenderer_->setInstanceTransform(transport.wmoInstanceId, transport.transform);
            // Tell the static-world floor query this deck is in motion, so it
            // only counts as a floor when it is underfoot. Set here rather than
            // once at register so it survives an instance rebuild from
            // streaming - the call is idempotent and cheap.
            wmoRenderer_->setInstanceIsTransport(transport.wmoInstanceId, true);
        }
    }
}

void TransportManager::updateTransformMatrices(ActiveTransport& transport) {
    // Convert position from canonical to render coordinates for WMO rendering
    // Canonical: +X=North, +Y=West, +Z=Up
    // Render: renderX=wowY (west), renderY=wowX (north), renderZ=wowZ (up)
    glm::vec3 renderPos = core::coords::canonicalToRender(transport.position);

    // Convert rotation from canonical to render space using proper basis change
    // Canonical → Render is a 90° CCW rotation around Z (swaps X and Y)
    // Proper formula: q_render = q_basis * q_canonical * q_basis^-1
    glm::quat basisRotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::quat basisInverse = glm::conjugate(basisRotation);
    glm::quat renderRot = basisRotation * transport.rotation * basisInverse;


    // Build transform matrix: translate * rotate * scale
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);
    glm::mat4 rotation = glm::mat4_cast(renderRot);
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));  // No scaling for transports

    transport.transform = translation * rotation * scale;
    transport.invTransform = glm::inverse(transport.transform);
}

void TransportManager::updateServerTransport(uint64_t guid, const glm::vec3& position, float orientation) {
    auto* transport = getTransport(guid);
    if (!transport) {
        LOG_WARNING("TransportManager::updateServerTransport: Transport not found: 0x", std::hex, guid, std::dec);
        return;
    }

    auto* pathEntry = pathRepo_.findPath(transport->pathId);

    // MO_TRANSPORT spawn orientation is the authored dock alignment. Preserve
    // it before TaxiPathNode assignment replaces the stationary spawn path;
    // route tangent yaw is correct underway but points the bow into the pier
    // during a repeated-position dwell.
    if (!transport->isM2 && !transport->worldCoords) {
        transport->dockYaw = orientation;
        transport->hasDockYaw = true;
    }

    if (!pathEntry || pathEntry->spline.durationMs() == 0) {
        // No path or stationary - handle directly before delegating to ClockSync.
        // Still track update count so future path assignments work.
        transport->serverUpdateCount++;
        transport->lastServerUpdate = elapsedTime_;
        transport->basePosition = position;
        transport->position = position;
        transport->rotation = glm::angleAxis(orientation, glm::vec3(0.0f, 0.0f, 1.0f));
        updateTransformMatrices(*transport);
        pushTransform(*transport);
        return;
    }

    if (transport->isM2 && isDeeprunTramTransport(*transport) && pathEntry->fromDBC && isPreWotlk()) {
        // CMangos sends occasional position echoes for Deeprun subway cars, but the client
        // owns the TransportAnimation.dbc path phase. Treat those samples as presence/yaw
        // hints rather than switching the M2 tram into stationary server-driven mode.
        const bool firstUpdate = transport->serverUpdateCount == 0;
        transport->serverUpdateCount++;
        transport->lastServerUpdate = elapsedTime_;
        transport->useClientAnimation = true;
        transport->clientAnimationReverse = false;
        transport->hasServerClock = false;
        const glm::vec3 baseDelta = position - transport->basePosition;
        const bool stationEcho = glm::dot(baseDelta, baseDelta) < 4.0f;
        if (firstUpdate || stationEcho) {
            if (!seedDeeprunTramStationPhase(*transport, *pathEntry, position)) {
                transport->basePosition = position;
                transport->position = position;
                transport->localClockMs = 0;
                transport->hasServerYaw = false;
                transport->rotation = glm::angleAxis(orientation, glm::vec3(0.0f, 0.0f, 1.0f));
            }
        }
        if (transport->serverUpdateCount <= 3) {
            LOG_DEBUG("Deeprun tram server update kept client-driven: guid=0x", std::hex, guid, std::dec,
                        " entry=", transport->entry,
                        " displayId=", transport->displayId,
                        " pathId=", transport->pathId,
                        " pos=(", position.x, ",", position.y, ",", position.z, ")",
                        " orientation=", orientation);
        }
        updateTransformMatrices(*transport);
        pushTransform(*transport);
        return;
    }

    // Delegate clock sync, yaw correction, and velocity bootstrap to ClockSync.
    // A borrowed-route transport is handed no path at all: it still gets the
    // server's position and the heading-vs-velocity yaw correction, and none of
    // the phase seeding that would put it back on someone else's route.
    clockSync_.processServerUpdate(*transport,
                                   transport->borrowedPath ? nullptr : pathEntry,
                                   position, orientation, elapsedTime_);

    updateTransformMatrices(*transport);
    pushTransform(*transport);
}

void TransportManager::rebindTransportInstance(uint64_t guid, uint32_t instanceId, bool isM2, uint32_t displayId) {
    auto* transport = getTransport(guid);
    if (!transport) return;

    const bool changed = transport->wmoInstanceId != instanceId ||
                         transport->isM2 != isM2 ||
                         (displayId != 0 && transport->displayId != displayId);
    transport->wmoInstanceId = instanceId;
    transport->isM2 = isM2;
    if (displayId != 0) {
        transport->displayId = displayId;
    }

    updateTransformMatrices(*transport);
    pushTransform(*transport);

    if (changed && isDeeprunTramTransport(*transport)) {
        LOG_INFO("Deeprun tram rebound to render instance: guid=0x", std::hex, guid, std::dec,
                    " instanceId=", instanceId,
                    " isM2=", isM2,
                    " displayId=", transport->displayId,
                    " pathId=", transport->pathId);
    }
}

bool TransportManager::loadTransportAnimationDBC(pipeline::AssetManager* assetMgr) {
    return pathRepo_.loadTransportAnimationDBC(assetMgr);
}

bool TransportManager::loadTaxiPathNodeDBC(pipeline::AssetManager* assetMgr) {
    return pathRepo_.loadTaxiPathNodeDBC(assetMgr);
}

bool TransportManager::hasTaxiPath(uint32_t taxiPathId) const {
    return pathRepo_.hasTaxiPath(taxiPathId);
}

bool TransportManager::hasTaxiPathForMap(uint32_t taxiPathId, uint32_t mapId) const {
    return pathRepo_.hasTaxiPathForMap(taxiPathId, mapId);
}

bool TransportManager::assignTaxiPathToTransport(uint32_t entry, uint32_t taxiPathId, uint32_t mapId) {
    auto* taxiEntry = pathRepo_.findTaxiPath(taxiPathId, mapId);
    if (!taxiEntry) {
        LOG_WARNING("No TaxiPathNode segment for taxiPathId=", taxiPathId, " on mapId=", mapId,
                    " (path exists on another map: ", pathRepo_.hasTaxiPath(taxiPathId), ")");
        return false;
    }

    // Assign to every transport with this entry. TaxiPathNode paths are absolute
    // world-coordinate routes (worldCoords=true, basePosition=0), so they are
    // independent of where the GO spawned. MO_TRANSPORT boats spawn at their dock
    // (a non-origin position), not at (0,0,0), so an origin-only gate here left
    // every continent-crossing ship without its route and falling back to an
    // unrelated TransportAnimation path (Deeprun Tram) instead.
    bool assignedAny = false;
    for (auto& [guid, transport] : transports_) {
        if (transport.entry != entry) continue;

        // Keep the authoritative spawn sample long enough to phase Kraken's
        // per-continent path segment. Seeding this route from wall-clock modulo
        // made its visual arrival unrelated to the server's map-transfer time.
        const glm::vec3 serverSpawnPosition = transport.position;

        // Copy the taxi path into the main paths (indexed by GO entry for this
        // transport), rebuilt at this hull's own speed when the server has
        // published a period to solve it from.
        //
        // The shared slice is timed at a nominal speed, and the authored dock
        // dwells do not scale with it. So a wrong speed does not merely make
        // the cycle the wrong length - the dwell lands in the wrong part of it,
        // and a phase the server publishes maps onto the wrong stretch of
        // route. Solved, the two timelines are proportional and the phase means
        // the same thing on both ends.
        const TaxiRoute* route = pathRepo_.findTaxiRoute(taxiPathId);
        PathEntry copied(taxiEntry->spline, entry, taxiEntry->zOnly,
                         /*dbc=*/false, taxiEntry->worldCoords);
        copied.presentMs = taxiEntry->presentMs;
        if (route && transport.hasServerRouteClock) {
            const float speed =
                TransportPathRepository::taxiRouteSpeedFor(*route, transport.routePeriodMs);
            if (speed > 0.0f) {
                PathEntry retimed =
                    TransportPathRepository::buildTaxiRouteSlice(*route, mapId, speed);
                if (retimed.spline.keyCount() >= 2) {
                    retimed.pathId = entry;
                    LOG_INFO("Transport entry=", entry, " route ", taxiPathId,
                             " retimed to the server's ", transport.routePeriodMs,
                             "ms period at ", speed, " yd/s");
                    copied = std::move(retimed);
                }
            }
        }
        pathRepo_.storePath(entry, std::move(copied));

        auto* storedEntry = pathRepo_.findPath(entry);

        // Update transport to use the new path
        transport.pathId = entry;
        transport.worldCoords = true;
        transport.borrowedPath = false;  // This route is its own now.
        transport.basePosition = glm::vec3(0.0f);  // World-coordinate path, no base offset
        transport.useClientAnimation = true;  // Server won't send position updates

        // Most legacy routes retain their established deterministic phase. Kraken
        // crosses map boundaries, so anchor each freshly loaded segment to the
        // server-authored spawn location; otherwise it can still be approaching
        // Borean Tundra when the server correctly transfers the rider back to map 0.
        if (storedEntry && storedEntry->spline.durationMs() > 0) {
            // Where the server says this hull is, not what time it happens to
            // be. A wall-clock modulo put the phase somewhere unrelated to the
            // route, so the zeppelin was departing its tower while the server
            // had it arriving at the other one. The server's own published
            // phase overrides this the moment it lands; this is what the hull
            // does until then, and the position the server spawned it at is
            // the only evidence there is.
            if (!transport.hasServerRouteClock && transport.serverUpdateCount > 0 &&
                !storedEntry->spline.keys().empty()) {
                const size_t nearest = storedEntry->spline.findNearestKey(serverSpawnPosition);
                transport.localClockMs = storedEntry->spline.keys()[nearest].timeMs;
            } else if (!transport.hasServerRouteClock) {
                transport.localClockMs = static_cast<uint32_t>(
                    nowEpochMs() % storedEntry->spline.durationMs());
            }
            transport.localClockMs %= storedEntry->spline.durationMs();
            transport.position = storedEntry->spline.evaluatePosition(transport.localClockMs);
            transport.onThisMap = storedEntry->presentAt(transport.localClockMs);
        }

        updateTransformMatrices(transport);
        pushTransform(transport);

        LOG_INFO("Assigned TaxiPathNode path to transport 0x", std::hex, guid, std::dec,
                 " entry=", entry, " taxiPathId=", taxiPathId,
                 " waypoints=", storedEntry ? storedEntry->spline.keyCount() : 0u,
                 " duration=", storedEntry ? storedEntry->spline.durationMs() : 0u, "ms",
                 " startPos=(", transport.position.x, ", ", transport.position.y, ", ", transport.position.z, ")");
        assignedAny = true;
    }

    if (!assignedAny) {
        LOG_DEBUG("No registered transport found for entry=", entry, " taxiPathId=", taxiPathId);
    }
    return assignedAny;
}

bool TransportManager::hasPathForEntry(uint32_t entry) const {
    return pathRepo_.hasPathForEntry(entry);
}

bool TransportManager::hasUsableMovingPathForEntry(uint32_t entry, float minXYRange) const {
    return pathRepo_.hasUsableMovingPathForEntry(entry, minXYRange);
}

uint32_t TransportManager::inferDbcPathForSpawn(const glm::vec3& spawnWorldPos,
                                               float maxDistance,
                                               bool allowZOnly) const {
    return pathRepo_.inferDbcPathForSpawn(spawnWorldPos, maxDistance, allowZOnly);
}

uint32_t TransportManager::inferMovingPathForSpawn(const glm::vec3& spawnWorldPos, float maxDistance) const {
    return pathRepo_.inferMovingPathForSpawn(spawnWorldPos, maxDistance);
}

uint32_t TransportManager::pickFallbackMovingPath(uint32_t entry, uint32_t displayId) const {
    return pathRepo_.pickFallbackMovingPath(entry, displayId);
}

} // namespace wowee::game
