// The model matrix a placement becomes.
//
// With no pitch and no roll every composition order is the same rotation, so an
// upright tree or a building on flat ground looks correct whichever order built
// it. Five attempts to settle the order were each judged against evidence that
// could not tell the candidates apart, and the order was wrong for all of them:
// Silverpine's chasm bridge - rot (0.5, 90.5, -11), a real roll across a ravine
// - sat at a visible slant.
//
// So this does not judge by eye, and it does not restate the code either. MODF
// stores, beside each building placement, the world-space bounding box
// Blizzard's own tools computed for it. Transform the WMO's root bounding box
// by the matrix under test and the two have to agree. The placements below are
// read from 3.3.5's ADTs and written down here so the check needs no assets.
//
// Over all 1469 placements in those ADTs with more than three degrees of pitch
// or roll, the composition this pins reproduces the stored bounds to zero error
// on every one; the next best candidate is out by a median of 4.3 yards and as
// much as 120.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/coordinates.hpp"
#include "rendering/placement_transform.hpp"

using wowee::rendering::placementModelMatrix;

namespace {

/// One MODF record: what it says, and what Blizzard's tools said it becomes.
struct Placement {
    const char* name;
    glm::vec3 position;      ///< MODF position, ADT axes
    glm::vec3 rotation;      ///< MODF rotation, degrees
    glm::vec3 localMin;      ///< the WMO's own MOHD bounding box
    glm::vec3 localMax;
    glm::vec3 worldMin;      ///< the bounding box MODF carries for this placement
    glm::vec3 worldMax;
};

/// Measured from World/Maps in a 3.3.5 install. Each has real pitch or roll -
/// the only shape that tells the six orders apart.
const Placement kPlacements[] = {
    // Silverpine's chasm bridge, the one that was visibly askew.
    {"DUSKWOODCHASMBRIDGE",
     {15705.8828f, 46.0014f, 16225.4629f}, {0.5000f, 90.5000f, -11.0000f},
     {-8.2299f, -19.2117f, -17.0310f}, {8.3799f, 19.2107f, 18.3602f},
     {15697.3398f, 25.5453f, 16203.0283f}, {15714.5850f, 67.7610f, 16247.6436f}},
    {"ROPEBRIDGE",
     {19629.6484f, 78.5727f, 19314.9102f}, {-1.5000f, -89.5000f, 3.0000f},
     {-53.1423f, -6.8178f, -16.1714f}, {49.5833f, 6.5153f, 10.5978f},
     {19579.5996f, 60.6812f, 19307.0820f}, {19683.1074f, 90.7912f, 19322.6973f}},
    // Thirty degrees on two axes at once, where the order shows up loudest.
    {"AHN_QIRAJ_COLUMND",
     {15318.5186f, 1.7197f, 26873.2129f}, {30.0000f, -18.5000f, -30.0000f},
     {-11.8378f, -15.7256f, -0.5236f}, {11.3757f, 10.8036f, 58.3235f},
     {15301.0537f, -9.0389f, 26862.5977f}, {15351.1426f, 58.1906f, 26916.1660f}},
    {"EXTERIOR_PIECE05",
     {15587.4033f, 17.8275f, 26724.7383f}, {15.5000f, -53.0000f, 14.5000f},
     {-71.3477f, -107.8577f, -25.6549f}, {45.1630f, 107.8576f, 34.1682f},
     {15483.3584f, -44.1994f, 26594.1113f}, {15708.5576f, 94.7943f, 26839.8027f}},
};

/// The axis-aligned box a placement's model bounds land in, in render axes.
///
/// The model's own axes reach the placement's as (y, z, x); adtToWorld carries
/// the placement's into the renderer's. Both are signed permutations, so a box
/// stays a box and the stored bounds can be compared corner for corner.
void transformedBounds(const Placement& p, glm::vec3& outMin, glm::vec3& outMax) {
    constexpr float kDeg = wowee::core::coords::PI / 180.0f;
    const glm::mat4 m = placementModelMatrix(
        wowee::core::coords::adtToWorld(p.position.x, p.position.y, p.position.z),
        glm::vec3(p.rotation.z * kDeg, p.rotation.x * kDeg,
                  (p.rotation.y + 180.0f) * kDeg),
        1.0f);

    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? p.localMax.x : p.localMin.x,
                               (i & 2) ? p.localMax.y : p.localMin.y,
                               (i & 4) ? p.localMax.z : p.localMin.z);
        const glm::vec3 world = glm::vec3(m * glm::vec4(corner, 1.0f));
        outMin = glm::min(outMin, world);
        outMax = glm::max(outMax, world);
    }
}

/// The stored bounds, carried into render axes the same way.
void storedBounds(const Placement& p, glm::vec3& outMin, glm::vec3& outMax) {
    const glm::vec3 a = wowee::core::coords::adtToWorld(p.worldMin.x, p.worldMin.y, p.worldMin.z);
    const glm::vec3 b = wowee::core::coords::adtToWorld(p.worldMax.x, p.worldMax.y, p.worldMax.z);
    outMin = glm::min(a, b);
    outMax = glm::max(a, b);
}

}  // namespace

TEST_CASE("a placement lands in the bounds its own MODF record carries", "[placement]") {
    for (const Placement& p : kPlacements) {
        INFO(p.name << " rot (" << p.rotation.x << ", " << p.rotation.y << ", "
                    << p.rotation.z << ")");
        glm::vec3 gotMin, gotMax, wantMin, wantMax;
        transformedBounds(p, gotMin, gotMax);
        storedBounds(p, wantMin, wantMax);
        // A tenth of a yard over boxes up to 200 across: the stored values are
        // rounded where they were written, not computed to a different rule.
        for (int axis = 0; axis < 3; ++axis) {
            INFO("axis " << axis);
            CHECK(gotMin[axis] == Catch::Approx(wantMin[axis]).margin(0.1));
            CHECK(gotMax[axis] == Catch::Approx(wantMax[axis]).margin(0.1));
        }
    }
}

TEST_CASE("a placement with no rotation is a translate and a scale", "[placement]") {
    const glm::mat4 m = placementModelMatrix({10.0f, -4.0f, 2.5f}, {0, 0, 0}, 3.0f);
    CHECK(m[3][0] == Catch::Approx(10.0f));
    CHECK(m[3][1] == Catch::Approx(-4.0f));
    CHECK(m[3][2] == Catch::Approx(2.5f));
    CHECK(m[0][0] == Catch::Approx(3.0f));
    CHECK(m[1][1] == Catch::Approx(3.0f));
    CHECK(m[2][2] == Catch::Approx(3.0f));
}

TEST_CASE("the rotation composes Z then Y then X", "[placement]") {
    // Built from explicit trigonometry rather than from glm::rotate, so this
    // checks the composition instead of restating it. The bounds check above is
    // what says the composition is the right one; this says it has not drifted.
    auto rotX = [](float a) {
        const float c = std::cos(a), s = std::sin(a);
        return glm::mat3(1, 0, 0, 0, c, s, 0, -s, c);
    };
    auto rotY = [](float a) {
        const float c = std::cos(a), s = std::sin(a);
        return glm::mat3(c, 0, -s, 0, 1, 0, s, 0, c);
    };
    auto rotZ = [](float a) {
        const float c = std::cos(a), s = std::sin(a);
        return glm::mat3(c, s, 0, -s, c, 0, 0, 0, 1);
    };

    // All three nonzero and all three different, because that is the only shape
    // that tells the six possible orders apart.
    const glm::vec3 euler(0.30f, -0.70f, 1.10f);
    const glm::mat3 expected = rotZ(euler.z) * rotY(euler.y) * rotX(euler.x);
    const glm::mat4 got = placementModelMatrix({0, 0, 0}, euler, 1.0f);

    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            INFO("column " << col << " row " << row);
            CHECK(got[col][row] == Catch::Approx(expected[col][row]).margin(1e-4));
        }
    }

    SECTION("and it is not the other way round") {
        CHECK_FALSE(glm::mat3(got) == rotX(euler.x) * rotY(euler.y) * rotZ(euler.z));
    }
}

TEST_CASE("scale is applied inside the rotation, not after it", "[placement]") {
    // Uniform scale commutes with rotation, so this cannot be caught by
    // comparing a rotated point. What it can be caught by is the translation:
    // scaling last would leave it untouched, and scaling the whole matrix
    // afterwards would multiply it.
    const glm::vec3 pos(7.0f, 0.0f, 0.0f);
    const glm::mat4 m = placementModelMatrix(pos, {0.2f, 0.3f, 0.4f}, 5.0f);
    CHECK(m[3][0] == Catch::Approx(7.0f));
    CHECK(m[3][1] == Catch::Approx(0.0f).margin(1e-6));
    CHECK(m[3][2] == Catch::Approx(0.0f).margin(1e-6));
}
