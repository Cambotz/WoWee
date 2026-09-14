#pragma once

/// The asset sets this client can be given, and what each one needs.
///
/// A profile is a named answer to "what do you want the game to look like". It
/// names one base extraction and any enrichments on top, and every step says
/// which source it needs - the game being extracted, a second installation to
/// borrow art from, or nothing at all.
///
/// This is the C++ side of what tools/asset_profiles.py holds for the scripts.
/// The two are kept the same deliberately: a profile is a promise about what a
/// name means, and a name that means two things is worse than two names.

#include <cstdint>
#include <string>
#include <vector>

namespace wowee::assets {

/// Where a step's input comes from.
enum class Source {
    None,       ///< something done to art already present
    Game,       ///< the pre-Warlords installation being extracted (MPQ)
    Borrow,     ///< a SECOND pre-Warlords installation, to take art from
    Later,      ///< Warlords or later, read as CASC
};

/// What a step does.
enum class Kind {
    Extract,        ///< read a game's archives into a tree of loose files
    Upscale,        ///< resample foliage textures into sidecars
    ImportModels,   ///< take models out of another installation
};

struct Step {
    Kind kind = Kind::Extract;
    Source source = Source::Game;
    std::string summary;    ///< what the player gets
    std::string detail;     ///< why, and what it costs
    std::string expansion;  ///< for Extract and ImportModels-from-Borrow
    std::vector<std::string> prefixes;  ///< for ImportModels
    int minutes = 5;
};

struct Profile {
    std::string id;
    std::string name;
    std::string summary;
    std::string detail;
    std::string expansion;  ///< the base game this is built from
    std::vector<Step> steps;

    [[nodiscard]] int minutes() const;
    /// Every source this profile needs that is not None.
    [[nodiscard]] std::vector<Source> sources() const;
};

/// Every profile, in the order they should be offered.
const std::vector<Profile>& profiles();

/// The profile with this id, or nullptr.
const Profile* profileById(const std::string& id);

/// Whether a profile can be built from the sources at hand.
///
/// `reason` is filled when it cannot, written for somebody who does not know
/// what CASC is and should not have to.
bool profileAvailable(const Profile& profile, bool haveGame, bool haveBorrow,
                      bool haveLater, std::string* reason);

}  // namespace wowee::assets
