#pragma once

#include <optional>

namespace wowee::rendering {

/// A swimmer's horizontal position. Only x and y: the swim clamp owns the
/// vertical, and this decides nothing about it.
struct SwimStep {
    float x = 0.0f;
    float y = 0.0f;
};

/// Where a swimmer ends up when the terrain ahead is a wall rather than a floor.
///
/// The swept collision a swimmer gets knows WMO walls and doodad collision, and
/// a shoreline is neither - it is the heightmap, which the swim path otherwise
/// only ever consults downward, for something to float above. So nothing stopped
/// a swimmer crossing into a hillside, and the floor probe does not push them
/// back out: it accepts a floor only at or just above the feet, and a cliff face
/// ahead of them is far above that. Leaving the water inside the hill then drops
/// them through the world.
///
/// `wallZ` is the height above which terrain counts as a wall rather than
/// something to float over - the swimmer's feet plus the clearance their float
/// gives them. A beach shelving up underneath stays passable; a cliff does not.
///
/// Each axis is tried alone before both are refused, so a swimmer meeting the
/// shore at an angle follows it along instead of sticking to it.
template <typename HeightAt>
SwimStep swimStepAgainstTerrain(const SwimStep& from, const SwimStep& to, float wallZ,
                                HeightAt&& heightAt) {
    const auto blocked = [&](float x, float y) {
        const std::optional<float> h = heightAt(x, y);
        return h.has_value() && *h > wallZ;
    };
    if (!blocked(to.x, to.y)) return to;
    if (!blocked(to.x, from.y)) return {.x = to.x, .y = from.y};
    if (!blocked(from.x, to.y)) return {.x = from.x, .y = to.y};
    return from;
}

}  // namespace wowee::rendering
