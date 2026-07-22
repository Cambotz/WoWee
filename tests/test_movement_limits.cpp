#include <catch_amalgamated.hpp>
#include "rendering/movement_limits.hpp"

TEST_CASE("stock hill climbing limits are shared by all surfaces") {
    using namespace wowee::rendering::movement;
    REQUIRE(kMaxWalkableSlopeDegrees == 50.0f);
    REQUIRE(isWalkableNormal(kMinWalkableNormalZ));
    REQUIRE_FALSE(isWalkableNormal(kMinWalkableNormalZ - 0.001f));
    REQUIRE(isReachableStep(kMaxStepUp));
    REQUIRE_FALSE(isReachableStep(kMaxStepUp + 0.001f));
}
