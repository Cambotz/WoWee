#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

struct ContentPackInfo {
    std::string name;
    std::string author;
    std::string description;
    std::string version = "1.0";
    uint32_t mapId = 9000;
    std::string format = "wcp-1.0";

    struct FileEntry {
        std::string path;        // path inside pack
        std::string category;    // terrain, object, creature, quest, texture, model
        uint64_t size = 0;
    };
    std::vector<FileEntry> files;
};

class ContentPacker {
public:
    // Pack all zone data from output directory into a .wcp file
    static bool packZone(const std::string& outputDir, const std::string& mapName,
                          const std::string& destPath, const ContentPackInfo& info);

    // Unpack a .wcp file to a directory
    static bool unpackZone(const std::string& wcpPath, const std::string& destDir);

    // Read pack info without extracting
    static bool readInfo(const std::string& wcpPath, ContentPackInfo& info);

    // Validate that a zone directory has all required open format files
    struct ValidationResult {
        bool hasWot = false, hasWhm = false, hasZoneJson = false;
        bool hasPng = false, hasWom = false, hasWob = false, hasWoc = false;
        bool hasCreatures = false, hasQuests = false, hasObjects = false;
        bool whmValid = false, womValid = false, wobValid = false, wocValid = false;
        // Counts of each format file (the has* booleans only tell whether
        // at least one exists; counts are useful for the --validate report).
        int wotCount = 0, whmCount = 0, womCount = 0, wobCount = 0;
        int wocCount = 0, pngCount = 0;
        // Counts of files that failed magic validation. A non-zero count
        // means the zone has at least one corrupted file; a player would
        // see missing geometry on load.
        int womInvalidCount = 0, wobInvalidCount = 0, wocInvalidCount = 0;
        int openFormatScore() const;
        std::string summary() const;
    };
    static ValidationResult validateZone(const std::string& zoneDir);
};

} // namespace editor
} // namespace wowee
