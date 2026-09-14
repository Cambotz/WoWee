#pragma once

/**
 * data_paths.hpp - where this client's assets live when nobody has said.
 *
 * Two programs need the same answer and did not have it. The client looked in
 * ~/Library/Application Support/Wowee/Data on macOS and nowhere in particular
 * anywhere else; the asset manager wrote to Data/ beside whatever directory the
 * terminal happened to be in. Run the manager from a home directory and it
 * spent eight minutes writing a complete extraction the client would never
 * look at, with nothing on screen saying where any of it had gone.
 *
 * So: one rule, in one place, that both read. Each platform's own per-user data
 * directory, which is where a user's data belongs on that platform and is
 * writable without asking for anything.
 *
 * This is only the default. WOW_DATA_PATH still wins for the client and the
 * asset manager still lets the folder be chosen; neither of those goes through
 * here.
 */

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee {
namespace core {

/// The per-user data directory for this client, or empty if the platform will
/// not say where one is.
inline std::filesystem::path userDataRoot() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0') {
        return fs::path(local) / "Wowee" / "Data";
    }
    if (const char* profile = std::getenv("USERPROFILE"); profile != nullptr && *profile != '\0') {
        return fs::path(profile) / "AppData" / "Local" / "Wowee" / "Data";
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return {};
#ifdef __APPLE__
    return fs::path(home) / "Library" / "Application Support" / "Wowee" / "Data";
#else
    // The XDG base directory spec, which is what a Linux or BSD desktop expects
    // and what a backup tool knows to pick up.
    if (const char* share = std::getenv("XDG_DATA_HOME"); share != nullptr && *share != '\0') {
        return fs::path(share) / "wowee" / "Data";
    }
    return fs::path(home) / ".local" / "share" / "wowee" / "Data";
#endif
#endif
}

/// The expansions extracted under a data root, by directory name, sorted.
///
/// Several games can be built into one data folder, each under its own name,
/// and both programs need to know which: the asset manager to say what is
/// already there and to name a pack holding more than one, the client to offer
/// the choice at its login screen. One answer, so the two cannot disagree about
/// what is installed.
///
/// The manifest is what the extractor writes last, so its presence is the
/// difference between a finished extraction and a directory somebody made -
/// or one a run that was stopped halfway left behind.
inline std::vector<std::string> installedExpansions(const std::filesystem::path& dataRoot) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    if (dataRoot.empty()) return out;

    std::error_code ec;
    const fs::path expansions = dataRoot / "expansions";
    for (fs::directory_iterator it(expansions, ec), end; it != end && !ec; it.increment(ec)) {
        if (!fs::is_regular_file(it->path() / "manifest.json", ec)) continue;
        out.push_back(it->path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// Whether a directory holds an extraction: a manifest at its root, or one in
/// any expansion beneath it.
inline bool holdsExtraction(const std::filesystem::path& dataRoot) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (dataRoot.empty()) return false;
    if (fs::is_regular_file(dataRoot / "manifest.json", ec)) return true;
    return !installedExpansions(dataRoot).empty();
}

}  // namespace core
}  // namespace wowee
