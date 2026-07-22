// coordinate_projection.hpp — Pure coordinate math for world map UV projection.
// Extracted from WorldMap methods (Phase 2 of refactoring plan).
// All functions are stateless free functions — trivially testable.
#pragma once

#include "rendering/world_map/world_map_types.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace wowee {
namespace rendering {
namespace world_map {

/// Project render-space position to [0,1] UV on a zone or continent map.
glm::vec2 renderPosToMapUV(const glm::vec3& renderPos,
                            const ZoneBounds& bounds,
                            bool isContinent);

/// Derive effective projection bounds for a continent from its child zones.
/// Uses zoneBelongsToContinent() internally. Returns false if insufficient data.
bool getContinentProjectionBounds(const std::vector<Zone>& zones,
                                   int contIdx,
                                   float& left, float& right,
                                   float& top, float& bottom);

/// Find the best-fit continent index for a player position.
/// Prefers the smallest containing continent; falls back to nearest center.
int findBestContinentForPlayer(const std::vector<Zone>& zones,
                                const glm::vec3& playerRenderPos);

/// Find the smallest zone (areaID != 0) containing the player position.
/// Returns -1 if no zone contains the position.
int findZoneForPlayer(const std::vector<Zone>& zones,
                       const glm::vec3& playerRenderPos);

/// Test if a zone spatially belongs to a given continent.
/// Uses parentWorldMapID when available, falls back to overlap heuristic.
bool zoneBelongsToContinent(const std::vector<Zone>& zones,
                             int zoneIdx, int contIdx);

/// Check whether the zone at idx is a root continent (has leaf continents as children).
bool isRootContinent(const std::vector<Zone>& zones, int idx);

/// Check whether the zone at idx is a leaf continent (parentWorldMapID != 0, areaID == 0).
bool isLeafContinent(const std::vector<Zone>& zones, int idx);

} // namespace world_map
} // namespace rendering
} // namespace wowee
