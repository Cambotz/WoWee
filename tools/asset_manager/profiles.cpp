#include "profiles.hpp"

namespace wowee::assets {

namespace {

Step extractStep(const std::string& expansion) {
    return Step{
        .kind = Kind::Extract,
        .source = Source::Game,
        .summary = "Extract the game you own",
        .detail = "Reads the archives and writes a tree of loose files. Nothing is "
                  "written into the game install itself.",
        .expansion = expansion,
        .prefixes = {},
        .minutes = 8,
    };
}

Step upscaleStep() {
    return Step{
        .kind = Kind::Upscale,
        .source = Source::None,
        .summary = "Sharpen the foliage",
        .detail = "Every texture of every foliage model, resampled and written as a "
                  "sidecar beside the original. Selection is by model rather than by "
                  "filename, so a trunk sheet comes along with the tree that uses it. "
                  "Reversible: the sidecars can be deleted and the originals are never "
                  "touched.",
        .expansion = {},
        .prefixes = {},
        .minutes = 25,
    };
}

Step cataTreesStep() {
    return Step{
        .kind = Kind::ImportModels,
        .source = Source::Borrow,
        .summary = "Cataclysm's trees, over Wrath's",
        .detail = "Cataclysm re-authored the trees the classic zones still use and left "
                  "them at the same paths. Stranglethorn's canopy is 156 vertices of "
                  "crossed planes in 3.3.5 and 897 vertices of leaf cards in 4.3.4.",
        .expansion = "cata",
        .prefixes = {"world"},
        .minutes = 12,
    };
}

Step legionCreaturesStep() {
    return Step{
        .kind = Kind::ImportModels,
        .source = Source::Later,
        .summary = "Legion's creatures and doodads",
        .detail = "Creature, world and item models from a Legion installation, converted "
                  "to the format this client reads. Proven: 784 of them run in a working "
                  "install. A model whose textures cannot be resolved is refused rather "
                  "than written half-finished.",
        .expansion = {},
        .prefixes = {"creature", "world", "item"},
        .minutes = 18,
    };
}

Step legionCharactersStep() {
    return Step{
        .kind = Kind::ImportModels,
        .source = Source::Later,
        .summary = "Legion's player character models (unproven)",
        .detail = "The reasons these were once refused do not survive measurement: "
                  "Legion's HumanMale has the same geoset groups as 3.3.5's, has the 1 "
                  "and 701 it was said to lack, is 7% more vertices rather than three "
                  "times, and its bone indices fit this client's format. What is "
                  "unsettled is whether its body UVs still match the skins CharSections "
                  "names. Try it and look - it can be switched off again.",
        .expansion = {},
        .prefixes = {"character"},
        .minutes = 10,
    };
}

std::vector<Profile> buildProfiles() {
    return {
        Profile{"wotlk", "Wrath of the Lich King",
                "The game as it shipped in 3.3.5a.",
                "What this client targets. Everything else here is built on top of it.",
                "wotlk", {extractStep("wotlk")}},

        Profile{"wotlk-sharp", "Wrath, with sharper foliage",
                "3.3.5a with crisper trees, bushes and grass.",
                "The same game, with the foliage textures resampled. The change is "
                "resolution only - nothing is re-authored and nothing moves.",
                "wotlk", {extractStep("wotlk"), upscaleStep()}},

        Profile{"wotlk-cata-trees", "Wrath, with Cataclysm trees",
                "3.3.5a, with the later client's trees where it has them.",
                "Cataclysm's foliage is a generation better and sits at the same paths, "
                "so it drops straight in. Needs a Cataclysm installation as well.",
                "wotlk", {extractStep("wotlk"), cataTreesStep(), upscaleStep()}},

        Profile{"wotlk-legion", "Wrath, with Legion models",
                "3.3.5a with Legion's creatures, doodads and sharper foliage.",
                "The furthest this goes while staying on ground that has been measured. "
                "Player characters are left as they shipped.",
                "wotlk", {extractStep("wotlk"), legionCreaturesStep(), upscaleStep()}},

        Profile{"wotlk-legion-characters", "Wrath, with Legion models and characters",
                "As above, and Legion's player models too. Unproven.",
                "Everything in the profile above, plus the player character models - the "
                "one part nobody has confirmed on screen yet.",
                "wotlk", {extractStep("wotlk"), legionCreaturesStep(),
                          legionCharactersStep(), upscaleStep()}},

        Profile{"tbc", "The Burning Crusade", "2.4.3, as it shipped.",
                "Extracted into its own tree, so it neither sees nor disturbs any other "
                "expansion you have extracted.",
                "tbc", {extractStep("tbc")}},

        Profile{"classic", "Classic", "1.12, as it shipped.",
                "Extracted into its own tree, so it neither sees nor disturbs any other "
                "expansion you have extracted.",
                "classic", {extractStep("classic")}},

        Profile{"turtle", "Turtle WoW", "The Turtle client's assets, 1.18.1.",
                "Turtle ships custom models and zones on a 1.12 base; this keeps them "
                "together in their own tree.",
                "turtle", {extractStep("turtle")}},
    };
}

}  // namespace

int Profile::minutes() const {
    int total = 0;
    for (const Step& step : steps) total += step.minutes;
    return total;
}

std::vector<Source> Profile::sources() const {
    std::vector<Source> out;
    for (const Step& step : steps) {
        if (step.source == Source::None) continue;
        bool seen = false;
        for (Source s : out) seen = seen || (s == step.source);
        if (!seen) out.push_back(step.source);
    }
    return out;
}

const std::vector<Profile>& profiles() {
    static const std::vector<Profile> kProfiles = buildProfiles();
    return kProfiles;
}

const Profile* profileById(const std::string& id) {
    for (const Profile& profile : profiles()) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

bool profileAvailable(const Profile& profile, bool haveGame, bool haveBorrow,
                      bool haveLater, std::string* reason) {
    // Borrowing is a source of its own, separate from the game being extracted.
    // Without that distinction "Wrath with Cataclysm trees" reads as buildable
    // to somebody who owns only Wrath.
    for (Source source : profile.sources()) {
        switch (source) {
            case Source::Game:
                if (!haveGame) {
                    if (reason) *reason = "Point at the game you want to extract first.";
                    return false;
                }
                break;
            case Source::Borrow:
                if (!haveBorrow) {
                    if (reason) *reason = "Needs a Cataclysm or Mists installation in the second box.";
                    return false;
                }
                break;
            case Source::Later:
                if (!haveLater) {
                    if (reason) *reason = "Needs a Legion or later installation in the second box.";
                    return false;
                }
                break;
            case Source::None:
                break;
        }
    }
    if (reason) reason->clear();
    return true;
}

}  // namespace wowee::assets
