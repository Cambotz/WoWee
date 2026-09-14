#pragma once

/// What a folder somebody pointed at actually is.
///
/// Told from the shape of the directory rather than from a version file, since
/// a version file is the first thing a repack changes. Pre-Warlords clients
/// keep their content in .MPQ archives; Warlords and later keep it in CASC,
/// which is a Data/data directory of .idx indices and numbered blobs.

#include <cstddef>
#include <string>

namespace wowee::assets {

enum class InstallKind {
    Unknown,    ///< nothing recognisable in there
    Missing,    ///< the path does not exist
    Mpq,        ///< classic through Mists: archives this can read
    Casc,       ///< Warlords onward: models only, see the note in describe()
};

struct InstallScan {
    InstallKind kind = InstallKind::Unknown;
    std::string dataDir;     ///< where the content actually lives
    std::string note;        ///< what to tell the person who chose it
    int archiveCount = 0;
    /// For a CASC install: how many files its root declares, once opened.
    /// Zero until somebody asks, since opening one reads an 80MB table.
    std::size_t fileCount = 0;
};

/// Look at a folder and say what it is, in words meant to be shown.
///
/// Cheap: it looks at the shape of the directory and opens nothing. Use
/// confirmCasc() to actually read one, which takes a second or so.
InstallScan scanInstall(const std::string& path);

/// Open a CASC install for real and report what is in it.
///
/// Separate from scanInstall because reading the encoding table is a second of
/// work and a window redrawing at sixty frames a second must not do it while
/// somebody is still typing the path.
bool confirmCasc(InstallScan& scan, std::string* error);

}  // namespace wowee::assets
