// What can be built, as a game and the upgrades that suit it.
//
// This was eight fixed combinations, and because every upgrade had to be spelled
// out against every base it applied to, most were only ever written against
// Wrath - so a Classic or Burning Crusade player was offered a plain extraction
// and nothing else, though the later clients improve their trees and creatures
// just as much.

#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "profiles.hpp"

using namespace wowee::assets;

namespace {

bool offers(const std::string& baseId, const std::string& upgradeId) {
    const Base* base = baseById(baseId);
    const Upgrade* upgrade = upgradeById(upgradeId);
    REQUIRE(base != nullptr);
    REQUIRE(upgrade != nullptr);
    return upgradeSuitsBase(*upgrade, *base);
}

bool hasStep(const Profile& profile, Kind kind) {
    return std::any_of(profile.steps.begin(), profile.steps.end(),
                       [kind](const Step& step) { return step.kind == kind; });
}

}  // namespace

TEST_CASE("every game can be built") {
    CHECK(bases().size() >= 4);
    for (const char* id : {"classic", "tbc", "wotlk", "cata"}) {
        INFO(id);
        CHECK(baseById(id) != nullptr);
    }
}

TEST_CASE("the upgrades are offered on every game they can improve") {
    // The point of the change: these are not Wrath-only, and never were.
    for (const char* base : {"classic", "tbc", "wotlk", "cata", "turtle"}) {
        INFO(base);
        CHECK(offers(base, "sharper"));
        CHECK(offers(base, "legion-models"));
        CHECK(offers(base, "legion-characters"));
    }
}

TEST_CASE("Cataclysm is not offered its own trees") {
    // Taking a client's models out of itself cannot improve anything, and
    // offering it reads as something being on the table that is not.
    CHECK_FALSE(offers("cata", "cata-trees"));
    CHECK(offers("wotlk", "cata-trees"));
    CHECK(offers("tbc", "cata-trees"));
    CHECK(offers("classic", "cata-trees"));
}

TEST_CASE("a base on its own is one extraction") {
    const Base* base = baseById("tbc");
    REQUIRE(base != nullptr);
    const Profile profile = assemble(*base, {});
    REQUIRE(profile.steps.size() == 1);
    CHECK(profile.steps[0].kind == Kind::Extract);
    CHECK(profile.steps[0].expansion == "tbc");
    CHECK(profile.expansion == "tbc");
}

TEST_CASE("the upscale runs after the models arrive") {
    const Base* base = baseById("wotlk");
    REQUIRE(base != nullptr);
    // Ticked in the other order, to show the order comes from the plan rather
    // than from how somebody happened to click.
    const Profile profile = assemble(*base, {"sharper", "legion-models"});

    REQUIRE(profile.steps.size() == 3);
    CHECK(profile.steps[0].kind == Kind::Extract);
    // The upscale resamples the textures of the models that are there. A model
    // arriving afterwards is a model whose textures nothing sharpened.
    CHECK(profile.steps[1].kind == Kind::ImportModels);
    CHECK(profile.steps[2].kind == Kind::Upscale);
}

TEST_CASE("an upgrade that does not suit the base is dropped, not carried") {
    const Base* base = baseById("cata");
    REQUIRE(base != nullptr);
    const Profile profile = assemble(*base, {"cata-trees", "sharper"});
    CHECK(hasStep(profile, Kind::Upscale));
    CHECK_FALSE(hasStep(profile, Kind::ImportModels));
}

TEST_CASE("what a profile needs is what its steps need") {
    const Base* base = baseById("wotlk");
    REQUIRE(base != nullptr);

    std::string why;
    // Borrowing is a source of its own, separate from the game being extracted.
    // Without that distinction "Cataclysm's trees" reads as available to
    // somebody who owns only Wrath.
    const Profile trees = assemble(*base, {"cata-trees"});
    CHECK_FALSE(profileAvailable(trees, /*game=*/true, /*borrow=*/false, /*later=*/false, &why));
    CHECK_FALSE(why.empty());
    CHECK(profileAvailable(trees, true, true, false, &why));

    const Profile legion = assemble(*base, {"legion-models"});
    CHECK_FALSE(profileAvailable(legion, true, true, false, &why));
    CHECK(profileAvailable(legion, true, false, true, &why));

    // And nothing at all is buildable without the game itself.
    CHECK_FALSE(profileAvailable(assemble(*base, {}), false, true, true, &why));
}

TEST_CASE("ticking more takes longer") {
    const Base* base = baseById("wotlk");
    REQUIRE(base != nullptr);
    CHECK(assemble(*base, {"sharper"}).minutes() > assemble(*base, {}).minutes());
}
