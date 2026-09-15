#pragma once

/// Finding the folder the archives are actually in.
///
/// Somebody who has just been told to "choose your World of Warcraft folder"
/// will point at whatever they think that is, and they are all reasonable
/// answers: the folder with Data inside it, the Data folder itself, the folder
/// they unzipped everything into which has the game one level down, or the
/// download folder with the whole thing still in a .zip.
///
/// So this looks rather than insists. It is deliberately free of everything
/// else the extractor knows - no archives are opened, no MPQ library is
/// linked - because the only question here is which directory to hand on, and
/// that is a question about directories.

#include <string>

namespace wowee::assets {

struct DataDirProbe {
    /// Where the .mpq files are, or empty if none were found.
    std::string dataDir;
    /// How many are in there.
    int archives = 0;
    /// Files that look like the game still packed up - .zip, .rar, .7z, an
    /// installer .exe - found anywhere in the search. A folder with those and
    /// no archives is somebody who has not unpacked their download yet, which
    /// is worth saying rather than "nothing here".
    int packed = 0;
    /// How many folders were looked in, for a message that can be acted on.
    int searched = 0;
};

/// Look for the Data folder at or under `root`.
///
/// The folder itself first, then a Data child - matched without regard to
/// case, because a repack unpacked on Linux may well have written `data` -
/// then breadth-first through what is below it, to `maxDepth`. The first
/// folder holding archives wins, and the walk stops early once it has found
/// one worth stopping for, so pointing this at a home directory costs a
/// bounded look rather than a full crawl.
[[nodiscard]] DataDirProbe findDataDir(const std::string& root, int maxDepth = 3);

}  // namespace wowee::assets
