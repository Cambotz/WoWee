#pragma once

/// Where the sun is, from the direction its light travels.
///
/// The lighting system gives a directional vector - the way the light goes -
/// so the sun is the other way. Below the horizon it stays below: this used to
/// be written inline in SkySystem::getSunPosition, which mirrored a sun under
/// the ground back up into the sky (`sunDir = dir` rather than `-dir`).
///
/// Nothing but the lens flare asks where the sun is, so all the mirror achieved
/// was to invent one for the flare to draw around, at a position no sun was at.
/// The flare's own height attenuation then read the mirrored height as a sun
/// climbing the sky and let it through unweakened.

#include <glm/glm.hpp>

namespace wowee::rendering {

/// The unit direction from the eye toward the sun.
///
/// A zero or degenerate directional means no sun has been given yet; straight
/// down is what the rest of the sky code falls back to, so the sun reads as
/// being straight up and everything that gates on height turns it off.
inline glm::vec3 sunDirectionFromLightDir(const glm::vec3& directionalDir) {
    const float lenSq = glm::dot(directionalDir, directionalDir);
    if (lenSq < 1e-8f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return -directionalDir * glm::inversesqrt(lenSq);
}

}  // namespace wowee::rendering
