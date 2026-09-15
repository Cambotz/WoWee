// Where the third-person camera pivots on the character it follows.
//
// The numbers below are measured off the shipped WotLK models: the height is
// the largest vertex Z in bind pose, the head is the pivot of the bone whose
// keyBoneId is 6. They are here because the fault this guards was invisible
// in the arithmetic and only showed up against real skeletons - every race in
// the game put the old pivot between 81% and 92% of the way up to its head
// bone, and a gnome put it at 126%, above the skull.
#include <catch_amalgamated.hpp>

#include "rendering/camera_controller.hpp"

using wowee::rendering::CameraController;
using Catch::Matchers::WithinAbs;

namespace {

struct Race {
    const char* name;
    float height;
    float headZ;
};

// A camera setting of 1.6 is the default, chosen against a human male.
constexpr float kDefault = 1.6f;

constexpr Race kHumanMale{"human male", 2.127f, 1.843f};
constexpr Race kGnomeMale{"gnome male", 1.325f, 0.752f};
constexpr Race kGnomeFemale{"gnome female", 1.174f, 0.702f};
constexpr Race kNightElfFemale{"night elf female", 2.291f, 2.042f};
constexpr Race kTaurenMale{"tauren male", 2.329f, 1.894f};
constexpr Race kDwarfMale{"dwarf male", 1.672f, 1.364f};
constexpr Race kDraeneiMale{"draenei male", 2.707f, 2.286f};

float pivot(const Race& race, float setting = kDefault) {
    return CameraController::pivotHeightFor(setting, race.height, race.headZ);
}

}  // namespace

TEST_CASE("the default leaves the character it was chosen against alone") {
    // 1.6 was picked by eye on a human, so a human's camera must not move.
    REQUIRE_THAT(pivot(kHumanMale), WithinAbs(1.6f, 0.01f));
}

TEST_CASE("the pivot stays below the head bone for every race") {
    // The old rule scaled the setting by total height, which is how a gnome
    // ended up with its pivot a quarter of a metre above its head bone: its
    // head and hair are 40% of its height where a human's are 13%.
    for (const Race& race : {kHumanMale, kGnomeMale, kGnomeFemale, kNightElfFemale,
                             kTaurenMale, kDwarfMale, kDraeneiMale}) {
        INFO(race.name);
        CHECK(pivot(race) < race.headZ);
        CHECK(pivot(race) > race.headZ * 0.75f);
    }
}

TEST_CASE("a gnome's pivot comes down from above its head") {
    // What the height-proportional rule gave: 1.325 * (1.6 / 2.13) = 0.995,
    // against a head bone at 0.752.
    CHECK(pivot(kGnomeMale) < 0.752f);
    CHECK_THAT(pivot(kGnomeMale), WithinAbs(0.653f, 0.01f));
    CHECK_THAT(pivot(kGnomeFemale), WithinAbs(0.609f, 0.01f));
}

TEST_CASE("the tall races keep roughly what they had") {
    // They were never the complaint, and a fix that moves them is a
    // regression dressed as one. Within 10cm of the old height-proportional
    // figures, which were themselves within a hand's width of the fixed 1.6.
    CHECK_THAT(pivot(kNightElfFemale), WithinAbs(1.772f, 0.05f));
    CHECK_THAT(pivot(kTaurenMale), WithinAbs(1.644f, 0.05f));
    CHECK_THAT(pivot(kDraeneiMale), WithinAbs(1.985f, 0.05f));
}

TEST_CASE("the setting still scales the pivot") {
    CHECK(pivot(kHumanMale, 2.0f) > pivot(kHumanMale, 1.6f));
    CHECK(pivot(kGnomeMale, 1.0f) < pivot(kGnomeMale, 1.6f));
    // Doubling the setting doubles the pivot, until the clamp.
    CHECK_THAT(pivot(kGnomeMale, 3.2f), WithinAbs(pivot(kGnomeMale, 1.6f) * 2.0f, 0.01f));
}

TEST_CASE("a skeleton with no head bone falls back to its height") {
    // Some models name no head - the tuskarr and one of the broken. They get
    // what they got before this, rather than the 0.2 floor.
    const float noHead = CameraController::pivotHeightFor(kDefault, 2.881f, 0.0f);
    CHECK_THAT(noHead, WithinAbs(2.881f * (kDefault / 2.13f), 0.01f));

    // And a model that has not been measured at all gets the setting whole.
    CHECK_THAT(CameraController::pivotHeightFor(kDefault, 0.0f, 0.0f),
               WithinAbs(kDefault, 0.001f));
}

TEST_CASE("the pivot is clamped to something a camera can use") {
    // A doll-sized instance must not pull the pivot to its feet, and a
    // world boss must not put it out of reach.
    CHECK(CameraController::pivotHeightFor(kDefault, 0.3f, 0.2f) >= 0.2f);
    CHECK(CameraController::pivotHeightFor(kDefault, 40.0f, 30.0f) <= 3.0f);
}

// ── the terrain lift ─────────────────────────────────────────
//
// The pivot the tests above choose was then raised again, by a rule written
// as an absolute height above the ground. That is what made all of the work
// above invisible on screen: it added back exactly what the pivot had moved.

namespace {
// A camera five metres back and pitched slightly down, which is what a player
// has after a couple of notches of the wheel.
constexpr float kBehind = 5.0f;
constexpr float kSlightlyAbove = 0.2f;  // the vertical part of pivot -> camera
}  // namespace

TEST_CASE("flat ground lifts nobody, whatever their height") {
    // The bug, stated as the thing it broke. Ground at the character's feet
    // is zero here, so a lift of anything at all is the camera being moved by
    // terrain that is not in the way.
    for (const Race& race : {kGnomeMale, kGnomeFemale, kHumanMale, kNightElfFemale, kDwarfMale}) {
        INFO(race.name);
        const float pivotZ = pivot(race);
        CHECK(CameraController::terrainPivotLift(pivotZ, kSlightlyAbove, kBehind, 0.0f)
              == Catch::Approx(0.0f));
    }
}

TEST_CASE("a hill behind the character raises the pivot") {
    // Standing at the foot of a slope. The camera is at 2.6 here - a 1.6
    // pivot plus a metre of pitch over five metres - so ground at 2.0 still
    // clears it and only something higher is in the way.
    const float pivotZ = pivot(kHumanMale);
    CHECK(CameraController::terrainPivotLift(pivotZ, kSlightlyAbove, kBehind, 2.0f)
          == Catch::Approx(0.0f));
    const float lift = CameraController::terrainPivotLift(pivotZ, kSlightlyAbove, kBehind, 2.5f);
    CHECK(lift > 0.0f);
    // And never more than the cap, or the camera leaves the character behind.
    CHECK(lift <= 1.4f);
}

TEST_CASE("the lift is what the clearance is short by") {
    // Camera at 1.0, ground at 0.8, so it clears by 0.2 and wants 0.6.
    const float lift = CameraController::terrainPivotLift(1.0f, 0.0f, 5.0f, 0.8f);
    CHECK(lift == Catch::Approx(0.4f));
}

TEST_CASE("a steep hill does not lift a gnome further than a human") {
    // Both are short of the same clearance by the same amount once the
    // ground is far enough above them, so the cap holds them together - the
    // lift is about the ground, not about the character.
    const float gnome = CameraController::terrainPivotLift(pivot(kGnomeMale), 0.0f, kBehind, 6.0f);
    const float human = CameraController::terrainPivotLift(pivot(kHumanMale), 0.0f, kBehind, 6.0f);
    CHECK(gnome == Catch::Approx(human));
    CHECK(gnome == Catch::Approx(1.4f));
}

TEST_CASE("looking up puts the camera near the ground, and lifts it") {
    // Pitched up steeply the camera swings down behind the character, which
    // is the other way it ends up in the dirt.
    const float pivotZ = pivot(kHumanMale);
    CHECK(CameraController::terrainPivotLift(pivotZ, -0.4f, kBehind, 0.0f) > 0.0f);
    // The same pitch with the camera close in is fine, because it has not
    // swung far.
    CHECK(CameraController::terrainPivotLift(pivotZ, -0.4f, 1.0f, 0.0f) == Catch::Approx(0.0f));
}
