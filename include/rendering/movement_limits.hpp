#pragma once

#include <cmath>

namespace wowee::rendering::movement {

// The retail client rejects ground steeper than 50 degrees and steps higher
// than roughly 0.6 yards. Keep these limits shared by terrain, WMO, and M2
// collision paths so release builds cannot silently diverge by surface type.
inline constexpr float kMaxWalkableSlopeDegrees = 50.0f;
inline constexpr float kMinWalkableNormalZ = 0.642787635f; // cos(50 degrees)
inline constexpr float kMaxStepUp = 0.60f;

inline bool isWalkableNormal(float normalZ) {
    return normalZ >= kMinWalkableNormalZ;
}

inline bool isReachableStep(float deltaZ) {
    return deltaZ >= -0.25f && deltaZ <= kMaxStepUp;
}

} // namespace wowee::rendering::movement
