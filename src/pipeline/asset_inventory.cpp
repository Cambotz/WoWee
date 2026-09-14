#include "pipeline/asset_inventory.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/data_paths.hpp"

namespace wowee {
namespace pipeline {
namespace {

namespace fs = std::filesystem;

/// The directories the client reads from. Not all of what an extraction holds -
/// just enough that their absence means something is wrong rather than that a
/// particular expansion happens not to have one.
constexpr std::array<const char*, 5> kNeeded = {
    "dbfilesclient", "interface", "world", "character", "creature",
};

/// A number that follows a key, read out of the head of a JSON file.
///
/// The extractor's manifest is thirty megabytes of entries and the count sits
/// in the first hundred bytes of it. Parsing the whole thing to learn one
/// number would make this too slow to run at startup, which is when it is
/// worth running.
uint64_t numberAfter(const fs::path& file, const char* key, std::size_t readAtMost) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return 0;
    std::string head(readAtMost, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));

    const std::size_t at = head.find(key);
    if (at == std::string::npos) return 0;
    std::size_t digits = head.find_first_of("0123456789", at + std::strlen(key));
    if (digits == std::string::npos) return 0;

    uint64_t value = 0;
    for (; digits < head.size() && head[digits] >= '0' && head[digits] <= '9'; ++digits) {
        value = value * 10 + uint64_t(head[digits] - '0');
    }
    return value;
}

/// A string that follows a key, same idea.
std::string stringAfter(const fs::path& file, const char* key) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const std::size_t at = text.find(key);
    if (at == std::string::npos) return {};
    const std::size_t open = text.find('"', at + std::strlen(key));
    if (open == std::string::npos) return {};
    const std::size_t close = text.find('"', open + 1);
    if (close == std::string::npos) return {};
    return text.substr(open + 1, close - open - 1);
}

/// How many textures the upscale pass wrote, from the manifest it keeps.
///
/// A key ending in .dds is a sidecar it wrote; the .blp beside it in the same
/// manifest is the source it read. Counting keys is counting entries without
/// parsing the JSON, and these manifests are kilobytes rather than the
/// megabytes the extraction manifest runs to.
///
/// `prefix` is empty for the manifest kept inside an expansion, whose keys are
/// already relative to it. The one at the data root covers every expansion at
/// once, so there it names the one being asked about - without that, a second
/// game installed beside the first is credited with all of the first's work.
uint64_t countUpscaled(const fs::path& manifest, const std::string& prefix) {
    std::ifstream in(manifest, std::ios::binary);
    if (!in) return 0;
    uint64_t count = 0;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t open = line.find('"');
        if (open == std::string::npos) continue;
        const std::size_t close = line.find('"', open + 1);
        if (close == std::string::npos || close < open + 5) continue;
        if (line.compare(close - 4, 4, ".dds") != 0) continue;
        if (!prefix.empty() && line.compare(open + 1, prefix.size(), prefix) != 0) continue;
        ++count;
    }
    return count;
}

/// Models an import wrote, which land in override/ beside the shipped ones.
uint64_t countImported(const fs::path& overrideDir) {
    std::error_code ec;
    uint64_t count = 0;
    for (fs::recursive_directory_iterator it(overrideDir, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string name = it->path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name.size() > 3 && name.compare(name.size() - 3, 3, ".m2") == 0) ++count;
    }
    return count;
}

std::string withThousands(uint64_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    int since = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (since == 3) { out.push_back(','); since = 0; }
        out.push_back(*it);
        ++since;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

}  // namespace

std::string AssetSet::summary() const {
    if (!complete) {
        return anyAssets ? name + " - an extraction that did not finish"
                         : name + " - nothing built for it yet";
    }

    std::string line = name + " - " + withThousands(fileCount) + " files";
    if (sharpenedTextures > 0) {
        line += ", " + withThousands(sharpenedTextures) + " sharpened textures";
    }
    if (importedModels > 0) {
        line += ", " + withThousands(importedModels) + " imported models";
    }
    if (!missing.empty()) {
        line += "  (missing ";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) line += ", ";
            line += missing[i];
        }
        line += ")";
    }
    return line;
}

bool AssetInventory::anyUsable() const {
    return std::any_of(sets.begin(), sets.end(),
                       [](const AssetSet& set) { return set.usable(); });
}

const AssetSet* AssetInventory::find(const std::string& id) const {
    const auto at = std::find_if(sets.begin(), sets.end(),
                                 [&id](const AssetSet& set) { return set.id == id; });
    return at == sets.end() ? nullptr : &*at;
}

std::string AssetInventory::troubleText() const {
    if (anyUsable()) return {};

    // A directory per expansion is shipped with the client whether or not
    // anyone has extracted that game, so "there are directories" is not the
    // same as "there is anything here".
    const bool anythingStarted =
        std::any_of(sets.begin(), sets.end(),
                    [](const AssetSet& set) { return set.anyAssets; });

    if (!anythingStarted) {
        return "No game assets in " + dataRoot +
               ".  Run wowee_assets to build them from a World of Warcraft "
               "installation you own, or to install a pack somebody sent you.";
    }

    // Something was started and none of it can be used, which is a different
    // problem and deserves a different sentence.
    std::string line = "The assets in " + dataRoot + " cannot be used: ";
    bool first = true;
    for (const AssetSet& set : sets) {
        if (!set.anyAssets) continue;
        if (!first) line += "; ";
        line += set.summary();
        first = false;
    }
    line += ".  Run wowee_assets to finish or rebuild them.";
    return line;
}

AssetInventory takeInventory(const std::string& dataRoot) {
    AssetInventory inventory;
    inventory.dataRoot = dataRoot;
    if (dataRoot.empty()) return inventory;

    std::error_code ec;
    const fs::path expansions = fs::path(dataRoot) / "expansions";
    for (fs::directory_iterator it(expansions, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;

        AssetSet set;
        set.id = it->path().filename().string();
        set.path = it->path().string();

        const fs::path profile = it->path() / "expansion.json";
        set.name = stringAfter(profile, "\"name\"");
        if (set.name.empty()) set.name = set.id;

        const fs::path manifest = it->path() / "manifest.json";
        set.complete = fs::is_regular_file(manifest, ec);
        if (set.complete) {
            set.fileCount = numberAfter(manifest, "\"fileCount\"", 4096);
        }

        for (const char* needed : kNeeded) {
            if (!fs::is_directory(it->path() / needed, ec)) set.missing.emplace_back(needed);
        }
        set.anyAssets = set.missing.size() < kNeeded.size();

        const fs::path overrides = it->path() / "override";
        if (fs::is_directory(overrides, ec)) {
            set.importedModels = countImported(overrides);
            set.sharpenedTextures += countUpscaled(overrides / "upscale_manifest.json", "");
        }
        // The upscale pass can also write its sidecars in place, keeping one
        // manifest at the data root for every expansion together - so a set
        // with no override directory may still have been sharpened.
        set.sharpenedTextures +=
            countUpscaled(fs::path(dataRoot) / "upscale_manifest.json", "expansions/" + set.id + "/");

        inventory.sets.push_back(std::move(set));
    }

    std::sort(inventory.sets.begin(), inventory.sets.end(),
              [](const AssetSet& a, const AssetSet& b) { return a.id < b.id; });
    return inventory;
}

}  // namespace pipeline
}  // namespace wowee
