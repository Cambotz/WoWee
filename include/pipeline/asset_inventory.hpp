#pragma once

/**
 * asset_inventory.hpp - what assets are actually installed, and whether they
 * are usable.
 *
 * The client used to find out the hard way. With nothing extracted it started
 * anyway, reported no expansions, fell back to a hardcoded default and then
 * failed somewhere far from the cause - a missing texture, a model that would
 * not load, a DBC lookup returning nothing. None of it said "there are no
 * assets here", and none of it said where it had looked.
 *
 * So this answers the question directly and early, in the terms somebody
 * building assets would recognise: which games are installed, whether each is
 * complete, and which upgrades were built into them.
 *
 * Cheap on purpose. The extractor's manifest is thirty megabytes and holds two
 * hundred thousand entries; nothing here parses it. The file count is read out
 * of its head, the upgrades are counted from the small manifests beside them,
 * and the rest is a handful of directory checks.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

/// One game's assets, as installed.
struct AssetSet {
    std::string id;        ///< the directory name, which is the expansion id
    std::string name;      ///< what a player calls it; the id if nothing says
    std::string path;

    /// The extractor writes its manifest last, so this is the difference
    /// between a finished extraction and one that was stopped partway.
    bool complete = false;
    uint64_t fileCount = 0;

    /// Upgrades built on top, as the asset manager offers them.
    uint64_t importedModels = 0;
    uint64_t sharpenedTextures = 0;

    /// Directories the client reads that are not there. A set can carry the
    /// manifest and still be missing these if somebody moved files about.
    std::vector<std::string> missing;

    /// Whether any art at all has been written here. Every expansion this
    /// client knows about has a directory holding its protocol definitions,
    /// shipped with the client and present whether or not anyone has ever
    /// extracted that game - so "no manifest" alone does not distinguish an
    /// extraction that was interrupted from one that was never started.
    bool anyAssets = false;

    [[nodiscard]] bool usable() const { return complete && missing.empty(); }

    /// One line, for somebody who wants to know what they have.
    [[nodiscard]] std::string summary() const;
};

/// Everything installed under one data root.
struct AssetInventory {
    std::string dataRoot;
    std::vector<AssetSet> sets;

    [[nodiscard]] bool anyUsable() const;
    /// The set with this id, or nullptr.
    [[nodiscard]] const AssetSet* find(const std::string& id) const;
    /// What to tell somebody who has nothing installed: where it looked, and
    /// what to run. Empty when there is something usable.
    [[nodiscard]] std::string troubleText() const;
};

/// Look at a data root and say what is in it.
AssetInventory takeInventory(const std::string& dataRoot);

}  // namespace pipeline
}  // namespace wowee
