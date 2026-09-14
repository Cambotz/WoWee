#include "install_scan.hpp"

#include "casc.hpp"
#include "../asset_extract/extractor.hpp"

#include <algorithm>
#include <filesystem>

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int countArchives(const fs::path& dir) {
    std::error_code ec;
    int found = 0;
    for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        if (lower(it->path().extension().string()) == ".mpq") ++found;
    }
    return found;
}

bool looksLikeCasc(const fs::path& dir) {
    std::error_code ec;
    // Data/data holds the .idx indices and the numbered blobs; Data/indices is
    // where some builds keep the former instead. Either is enough to know.
    return fs::is_directory(dir / "Data" / "data", ec) ||
           fs::is_directory(dir / "Data" / "indices", ec) ||
           fs::is_directory(dir / "data", ec);
}

}  // namespace

InstallScan scanInstall(const std::string& path) {
    InstallScan out;
    if (path.empty()) return out;

    std::error_code ec;
    const fs::path root(path);
    if (!fs::is_directory(root, ec)) {
        out.kind = InstallKind::Missing;
        out.note = "That folder does not exist.";
        return out;
    }

    // Somebody may hand over the game folder or the Data folder inside it, and
    // both are the obvious thing to hand over. Accept either.
    for (const fs::path& candidate : {root, root / "Data"}) {
        if (!fs::is_directory(candidate, ec)) continue;
        const int archives = countArchives(candidate);
        if (archives > 0) {
            out.kind = InstallKind::Mpq;
            out.dataDir = candidate.string();
            out.archiveCount = archives;

            // Which game it is, from the archives that are there. Worth saying
            // rather than making somebody pick it off a list: the answer is in
            // the folder they just chose, and a person who mis-picks it builds
            // the wrong thing for eight minutes before finding out.
            out.expansion = tools::Extractor::detectExpansion(out.dataDir);
            out.note = out.expansion.empty()
                           ? "Found " + std::to_string(archives) +
                                 " archives here, but not which game they are from."
                           : "Found a game here - " + std::to_string(archives) +
                                 " archives, readable.";
            return out;
        }
    }

    if (looksLikeCasc(root)) {
        out.kind = InstallKind::Casc;
        out.dataDir = root.string();
        out.note = "This is Warlords or later. Those can supply models, but not a whole "
                   "game: none of their data tables are in a form this client reads, and "
                   "the terrain is later geography besides.";
        return out;
    }

    out.kind = InstallKind::Unknown;
    out.note = "No game archives in there. Choose the folder that has Data inside it, or "
               "the Data folder itself.";
    return out;
}

}  // namespace wowee::assets

namespace wowee::assets {

bool confirmCasc(InstallScan& scan, std::string* error) {
    if (scan.kind != InstallKind::Casc) {
        if (error) *error = "not a CASC installation";
        return false;
    }
    CascStorage storage;
    if (!storage.open(scan.dataDir, error)) return false;
    scan.fileCount = storage.rootCount();
    scan.note = "Read it: " + std::to_string(scan.fileCount) +
                " files. Models can be taken from here - not a whole game, since "
                "none of its data tables are in a form this client reads.";
    return true;
}

}  // namespace wowee::assets
