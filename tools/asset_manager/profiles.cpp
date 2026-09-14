#include "profiles.hpp"

#include <algorithm>

namespace wowee::assets {

namespace {

Step extractStep(const Base& base) {
    return Step{
        .kind = Kind::Extract,
        .source = Source::Game,
        .summary = "Extract " + base.name,
        .detail = "Reads the archives and writes a tree of loose files. Nothing is "
                  "written into the game install itself.",
        .expansion = base.expansion,
        .prefixes = {},
        .minutes = base.minutes,
    };
}

}  // namespace

const std::vector<Base>& bases() {
    static const std::vector<Base> all = {
        Base{"wotlk", "Wrath of the Lich King", "3.3.5a",
             "What this client targets, and what most private servers run.",
             "wotlk", 8},
        Base{"cata", "Cataclysm", "4.3.4",
             "The re-authored old world, with the flying and the changed geography.",
             "cata", 10},
        Base{"tbc", "The Burning Crusade", "2.4.3",
             "Outland, and the old world as it was before Cataclysm moved it.",
             "tbc", 6},
        Base{"classic", "Vanilla", "1.12",
             "The old world before either expansion touched it.",
             "classic", 5},
        Base{"turtle", "Turtle WoW", "1.18.1",
             "A vanilla client with custom zones and models of its own.",
             "turtle", 6},
    };
    return all;
}

const std::vector<Upgrade>& upgrades() {
    static const std::vector<Upgrade> all = {
        Upgrade{
            .id = "sharper",
            .name = "Sharper foliage",
            .summary = "Crisper trees, bushes and grass.",
            .detail = "Every texture of every foliage model, resampled and written as a "
                      "sidecar beside the original. Selection is by model rather than by "
                      "filename, so a trunk sheet comes along with the tree that uses it. "
                      "Reversible: the sidecars can be deleted and the originals are never "
                      "touched.",
            .kind = Kind::Upscale,
            .source = Source::None,
            .minutes = 25,
        },
        Upgrade{
            .id = "cata-trees",
            .name = "Cataclysm's trees and doodads",
            .summary = "The later client's foliage, where it re-authored it.",
            .detail = "Cataclysm re-authored the trees the old zones use and left them at "
                      "the same paths, so they drop straight in. Stranglethorn's canopy is "
                      "156 vertices of crossed planes in 3.3.5 and 897 of leaf cards in "
                      "4.3.4. Needs a Cataclysm installation as well as the one being "
                      "extracted.",
            .kind = Kind::ImportModels,
            .source = Source::Borrow,
            .expansion = "cata",
            .prefixes = {"world"},
            .minutes = 12,
            // Cataclysm already has them, and taking a client's models out of
            // itself is work that cannot improve anything.
            .notFor = {"cata"},
        },
        Upgrade{
            .id = "legion-models",
            .name = "Legion's creatures and doodads",
            .summary = "Creature, world and item models from a modern client.",
            .detail = "Models from a Legion installation, converted to the format this "
                      "client reads: the wrapper unwrapped, the version restamped, and the "
                      "skins and animations the later format keeps in separate files "
                      "brought along. Only where the later model is meaningfully bigger "
                      "than the one already there, and a model whose textures cannot be "
                      "resolved is refused rather than written half-finished.",
            .kind = Kind::ImportModels,
            .source = Source::Later,
            .prefixes = {"creature", "world", "item"},
            .minutes = 18,
        },
        Upgrade{
            .id = "legion-characters",
            .name = "Legion's player character models",
            .summary = "The new player models. Nobody has confirmed these on screen yet.",
            .detail = "The reasons these were once refused do not survive measurement: "
                      "Legion's HumanMale has the same geoset groups as 3.3.5's, has the 1 "
                      "and 701 it was said to lack, is 7% more vertices rather than three "
                      "times, and its bone indices fit this client's format. What is "
                      "unsettled is whether its body UVs still match the skins CharSections "
                      "names. Try it and look - it can be switched off again.",
            .kind = Kind::ImportModels,
            .source = Source::Later,
            .prefixes = {"character"},
            .minutes = 12,
        },
    };
    return all;
}

const Base* baseById(const std::string& id) {
    for (const Base& base : bases()) {
        if (base.id == id) return &base;
    }
    return nullptr;
}

const Upgrade* upgradeById(const std::string& id) {
    for (const Upgrade& upgrade : upgrades()) {
        if (upgrade.id == id) return &upgrade;
    }
    return nullptr;
}

bool upgradeSuitsBase(const Upgrade& upgrade, const Base& base) {
    return std::find(upgrade.notFor.begin(), upgrade.notFor.end(), base.id) ==
           upgrade.notFor.end();
}

Profile assemble(const Base& base, const std::vector<std::string>& upgradeIds) {
    Profile profile;
    profile.id = base.id;
    profile.name = base.name;
    profile.summary = base.patch;
    profile.detail = base.detail;
    profile.expansion = base.expansion;
    profile.steps.push_back(extractStep(base));

    // In the order they are declared rather than the order they were ticked, so
    // the plan reads the same whichever way somebody arrived at it. Models
    // before the upscale either way: the upscale resamples the textures of the
    // models that are there, and a model that arrives afterwards is a model
    // whose textures nothing sharpened.
    for (const Upgrade& upgrade : upgrades()) {
        if (std::find(upgradeIds.begin(), upgradeIds.end(), upgrade.id) == upgradeIds.end()) {
            continue;
        }
        if (!upgradeSuitsBase(upgrade, base)) continue;
        profile.id += "-" + upgrade.id;
        profile.steps.push_back(Step{
            .kind = upgrade.kind,
            .source = upgrade.source,
            .summary = upgrade.name,
            .detail = upgrade.detail,
            .expansion = upgrade.expansion,
            .prefixes = upgrade.prefixes,
            .minutes = upgrade.minutes,
        });
    }

    std::stable_sort(profile.steps.begin() + 1, profile.steps.end(),
                     [](const Step& a, const Step& b) {
                         return int(a.kind == Kind::Upscale) < int(b.kind == Kind::Upscale);
                     });
    return profile;
}

int Profile::minutes() const {
    int total = 0;
    for (const Step& step : steps) total += step.minutes;
    return total;
}

std::vector<Source> Profile::sources() const {
    std::vector<Source> out;
    for (const Step& step : steps) {
        if (step.source == Source::None) continue;
        if (std::find(out.begin(), out.end(), step.source) == out.end()) {
            out.push_back(step.source);
        }
    }
    return out;
}

bool sourceAvailable(Source source, bool haveGame, bool haveBorrow, bool haveLater,
                     std::string* reason) {
    // Borrowing is a source of its own, separate from the game being extracted.
    // Without that distinction "Cataclysm's trees" reads as available to
    // somebody who owns only Wrath.
    switch (source) {
        case Source::Game:
            if (!haveGame) {
                if (reason) *reason = "Point at the game you want to extract first.";
                return false;
            }
            break;
        case Source::Borrow:
            if (!haveBorrow) {
                if (reason) *reason = "Needs a Cataclysm installation as well.";
                return false;
            }
            break;
        case Source::Later:
            if (!haveLater) {
                if (reason) *reason = "Needs a Legion or later installation as well.";
                return false;
            }
            break;
        case Source::None:
            break;
    }
    if (reason) reason->clear();
    return true;
}

bool profileAvailable(const Profile& profile, bool haveGame, bool haveBorrow,
                      bool haveLater, std::string* reason) {
    for (Source source : profile.sources()) {
        if (!sourceAvailable(source, haveGame, haveBorrow, haveLater, reason)) return false;
    }
    if (reason) reason->clear();
    return true;
}

}  // namespace wowee::assets
