#include "install_probe.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// A download that has not been unpacked yet, by the look of its name.
bool looksPacked(const std::string& extension) {
    return extension == ".zip" || extension == ".rar" || extension == ".7z" ||
           extension == ".exe" || extension == ".gz" || extension == ".tar" ||
           extension == ".iso" || extension == ".dmg";
}

/// How many folders to look in before giving up.
///
/// Somebody may hand over a home directory by mistake, and a full crawl of one
/// takes long enough to look like a hang. Generous enough for any shape of
/// install - a wrapper folder, a folder of wrapper folders - and small enough
/// to be over in an instant.
constexpr int kMaxFolders = 512;

/// Enough archives to stop looking. A real Data folder has several: the
/// smallest supported install, vanilla, has a dozen, and every later one has
/// more. One .mpq on its own might be a patch somebody saved next to their
/// downloads, so it is remembered but not taken as the answer.
constexpr int kConfident = 3;

}  // namespace

DataDirProbe findDataDir(const std::string& root, int maxDepth) {
    DataDirProbe out;
    if (root.empty()) return out;

    std::error_code ec;
    const fs::path start(root);
    if (!fs::is_directory(start, ec)) return out;

    // Breadth first, so the shallowest answer wins: a folder holding both the
    // game and a backup copy of its Data folder should resolve to the one the
    // person meant, which is the one nearest what they chose.
    std::deque<std::pair<fs::path, int>> queue{{start, 0}};
    std::string best;
    int bestCount = 0;

    while (!queue.empty() && out.searched < kMaxFolders) {
        auto [dir, depth] = queue.front();
        queue.pop_front();
        ++out.searched;

        int archives = 0;
        std::vector<fs::path> children;
        for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
            const fs::path& entry = it->path();
            if (it->is_directory(ec)) {
                children.push_back(entry);
                continue;
            }
            const std::string extension = lower(entry.extension().string());
            if (extension == ".mpq") {
                ++archives;
            } else if (looksPacked(extension)) {
                ++out.packed;
            }
        }

        if (archives > bestCount) {
            bestCount = archives;
            best = dir.string();
            if (archives >= kConfident) break;
        }

        if (depth >= maxDepth) continue;
        // A folder named Data goes to the front of the queue whatever else is
        // beside it: it is where the archives live in every install anybody
        // has ever shipped, and matched without case because an unpacked
        // repack on a case-sensitive filesystem may spell it any way at all.
        std::stable_sort(children.begin(), children.end(),
                         [](const fs::path& a, const fs::path& b) {
                             return lower(a.filename().string()) == "data" &&
                                    lower(b.filename().string()) != "data";
                         });
        for (const fs::path& child : children) queue.emplace_back(child, depth + 1);
    }

    out.dataDir = best;
    out.archives = bestCount;
    return out;
}

}  // namespace wowee::assets
