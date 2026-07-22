#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

namespace wowee {
namespace tools {

/**
 * Extraction pipeline: MPQ archives → loose files + manifest
 */
class Extractor {
public:
    struct Options {
        std::string mpqDir;       // Path to WoW Data directory
        std::string outputDir;    // Output directory for extracted assets
        std::string expansion;    // "classic", "turtle", "tbc", "wotlk", or "" for auto-detect
        std::string locale;       // "enUS", "deDE", etc., or "" for auto-detect
        bool expansionSubdir = false; // Write under outputDir/expansions/<expansion>
        int threads = 0;          // 0 = auto-detect
        bool verify = false;      // CRC32 verify after extraction
        bool verbose = false;     // Verbose logging
        bool generateDbcCsv = false; // Convert selected DBFilesClient/*.dbc to CSV for committing
        bool skipDbcExtraction = false; // Extract visual assets only (recommended when CSV DBCs are in repo)
        bool onlyUsedDbcs = false; // Extract only the DBC files wowee uses (implies DBFilesClient/*.dbc filter)
        std::string dbcCsvOutputDir; // When set, write CSVs into this directory instead of outputDir/expansions/<exp>/db
        std::string referenceManifest; // If set, only extract files NOT in this manifest (delta extraction)
        std::string listFile;         // External listfile for MPQ enumeration (resolves unnamed hash entries)
        // Open-format emission: post-extract pass that writes wowee
        // open-format side-files (e.g. foo.blp → foo.png) without
        // touching the original. Lets wowee's runtime/editor consume
        // the open formats while keeping the proprietary copies that
        // private servers (AzerothCore/TrinityCore) read from.
        bool emitPng = false;          // BLP → PNG side-files
        bool emitJsonDbc = false;      // DBC → JSON side-files
        bool emitWom = false;          // M2 (+skin) → WOM side-files
        bool emitWob = false;          // WMO (+groups) → WOB side-files
        bool emitTerrain = false;      // ADT → WHM + WOT + WOC side-files
    };

    struct Stats {
        std::atomic<uint64_t> filesExtracted{0};
        std::atomic<uint64_t> bytesExtracted{0};
        std::atomic<uint64_t> filesSkipped{0};
        std::atomic<uint64_t> filesFailed{0};
    };

    /**
     * Auto-detect expansion from files in mpqDir.
     * @return "classic", "turtle", "tbc", "wotlk", or "" if unknown
     */
    static std::string detectExpansion(const std::string& mpqDir);

    /**
     * Auto-detect locale by scanning for locale subdirectories.
     * @return locale string like "enUS", or "" if none found
     */
    static std::string detectLocale(const std::string& mpqDir);

    /**
     * Run the extraction pipeline
     * @return true on success
     */
    static bool run(const Options& opts);

private:
    static bool enumerateFiles(const Options& opts,
                               std::vector<std::string>& outFiles);
};

} // namespace tools
} // namespace wowee
