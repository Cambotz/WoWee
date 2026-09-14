#pragma once

/// What can be built, as two questions rather than one list.
///
/// This used to be eight fixed combinations, named things like "Wrath, with
/// Cataclysm trees". Every upgrade had to be spelled out against every base it
/// applied to, so most of them were only ever offered on Wrath - and a Classic
/// or Burning Crusade player, whose trees and creatures the later clients
/// improve just as much, was offered a plain extraction and nothing else.
///
/// So: which game your server runs, and then which upgrades you want on top of
/// it. The upgrades that make no sense for a base are not offered for it, and
/// the ones that need a second installation say which.

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

/// A game to build from: whichever one the server runs.
struct Base {
    std::string id;
    std::string name;       ///< what a player calls it
    std::string patch;      ///< the version a server would name
    std::string detail;
    std::string expansion;  ///< what the extractor calls it
    int minutes = 8;
};

/// Something optional, on top of a base.
struct Upgrade {
    std::string id;
    std::string name;
    std::string summary;    ///< one line, what you get
    std::string detail;
    Kind kind = Kind::Upscale;
    Source source = Source::None;
    std::string expansion;              ///< which installation to borrow from
    std::vector<std::string> prefixes;  ///< which subtrees to take
    int minutes = 10;
    /// Bases this is worth offering for. Empty means all of them.
    std::vector<std::string> notFor;
};

/// The assembled job: one base and whatever was ticked.
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

/// The games that can be built from, newest first.
const std::vector<Base>& bases();

/// Everything that can be put on top of one.
const std::vector<Upgrade>& upgrades();

const Base* baseById(const std::string& id);
const Upgrade* upgradeById(const std::string& id);

/// Whether this upgrade is worth offering for this base. Cataclysm's trees are
/// nothing to a Cataclysm install, which already has them.
bool upgradeSuitsBase(const Upgrade& upgrade, const Base& base);

/// The job a base and a set of ticked upgrades add up to.
Profile assemble(const Base& base, const std::vector<std::string>& upgradeIds);

/// Whether a step's input is to hand.
///
/// `reason` is filled when it is not, written for somebody who does not know
/// what CASC is and should not have to.
bool sourceAvailable(Source source, bool haveGame, bool haveBorrow, bool haveLater,
                     std::string* reason);

/// Whether everything this profile needs is to hand.
bool profileAvailable(const Profile& profile, bool haveGame, bool haveBorrow,
                      bool haveLater, std::string* reason);

}  // namespace wowee::assets
