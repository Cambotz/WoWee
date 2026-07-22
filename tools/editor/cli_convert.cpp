#include "cli_convert.hpp"
#include "cli_subprocess.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleConvertM2Batch(int& i, int argc, char** argv) {
    // Bulk M2→WOM conversion. Walks <srcDir> recursively for
    // every .m2 file and re-invokes --convert-m2 per file via
    // a child process so the existing single-file logic (with
    // its AssetManager + skin-resolution bookkeeping) is reused
    // verbatim. Reports per-file pass/fail and an aggregate
    // summary.
    //
    // Designed to migrate an entire creature/world model dump
    // in one go. Pair with --convert-blp-batch and --convert-
    // wmo-batch to migrate a complete extracted Data tree.
    std::string srcDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "convert-m2-batch: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    std::vector<std::string> m2Files;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext != ".m2") continue;
        m2Files.push_back(e.path().string());
    }
    std::sort(m2Files.begin(), m2Files.end());
    std::printf("convert-m2-batch: %s\n", srcDir.c_str());
    std::printf("  candidates : %zu .m2 file(s)\n", m2Files.size());
    std::string self = argv[0];
    int ok = 0, failed = 0;
    for (const auto& m2 : m2Files) {
        std::fflush(stdout);
        int rc = wowee::editor::cli::runChild(self,
            {"--convert-m2", m2}, /*quiet=*/true);
        if (rc == 0) {
            ok++;
            std::printf("  [ok]   %s\n", m2.c_str());
        } else {
            failed++;
            std::printf("  [FAIL] %s (rc=%d)\n", m2.c_str(), rc);
        }
    }
    std::printf("\n  summary    : %d ok, %d failed (out of %zu)\n",
                ok, failed, m2Files.size());
    return failed == 0 ? 0 : 1;
}

int handleConvertWmoBatch(int& i, int argc, char** argv) {
    // Bulk WMO→WOB conversion. Same orchestrator pattern as
    // --convert-m2-batch: walks <srcDir> recursively, runs the
    // existing single-file --convert-wmo per file.
    //
    // Skips group files (e.g. Stormwind_001.wmo) since the
    // root WMO converter already pulls those in transitively.
    // A WMO is a "group file" iff its stem ends in _NNN where
    // NNN is a 3-digit integer.
    std::string srcDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "convert-wmo-batch: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    auto isGroupFile = [](const std::string& stem) {
        if (stem.size() < 5) return false;
        if (stem[stem.size() - 4] != '_') return false;
        for (int k = 1; k <= 3; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(
                    stem[stem.size() - k]))) return false;
        }
        return true;
    };
    std::vector<std::string> wmoFiles;
    int skippedGroups = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext != ".wmo") continue;
        std::string stem = e.path().stem().string();
        if (isGroupFile(stem)) { skippedGroups++; continue; }
        wmoFiles.push_back(e.path().string());
    }
    std::sort(wmoFiles.begin(), wmoFiles.end());
    std::printf("convert-wmo-batch: %s\n", srcDir.c_str());
    std::printf("  candidates  : %zu root .wmo file(s) (skipped %d group file(s))\n",
                wmoFiles.size(), skippedGroups);
    std::string self = argv[0];
    int ok = 0, failed = 0;
    for (const auto& wmo : wmoFiles) {
        std::fflush(stdout);
        int rc = wowee::editor::cli::runChild(self,
            {"--convert-wmo", wmo}, /*quiet=*/true);
        if (rc == 0) {
            ok++;
            std::printf("  [ok]   %s\n", wmo.c_str());
        } else {
            failed++;
            std::printf("  [FAIL] %s (rc=%d)\n", wmo.c_str(), rc);
        }
    }
    std::printf("\n  summary     : %d ok, %d failed (out of %zu)\n",
                ok, failed, wmoFiles.size());
    return failed == 0 ? 0 : 1;
}

int handleConvertBlpBatch(int& i, int argc, char** argv) {
    // Bulk BLP→PNG conversion. Walks <srcDir> recursively for
    // every .blp file and re-invokes --convert-blp-png per
    // file via a child process. The single-file converter
    // writes the .png as a sidecar next to the source by
    // default, so a batched run mirrors the standard "PNG
    // sidecar everywhere" layout.
    std::string srcDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "convert-blp-batch: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    std::vector<std::string> blpFiles;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext != ".blp") continue;
        blpFiles.push_back(e.path().string());
    }
    std::sort(blpFiles.begin(), blpFiles.end());
    std::printf("convert-blp-batch: %s\n", srcDir.c_str());
    std::printf("  candidates : %zu .blp file(s)\n", blpFiles.size());
    std::string self = argv[0];
    int ok = 0, failed = 0;
    for (const auto& blp : blpFiles) {
        std::fflush(stdout);
        int rc = wowee::editor::cli::runChild(self,
            {"--convert-blp-png", blp}, /*quiet=*/true);
        if (rc == 0) {
            ok++;
            std::printf("  [ok]   %s\n", blp.c_str());
        } else {
            failed++;
            std::printf("  [FAIL] %s (rc=%d)\n", blp.c_str(), rc);
        }
    }
    std::printf("\n  summary    : %d ok, %d failed (out of %zu)\n",
                ok, failed, blpFiles.size());
    return failed == 0 ? 0 : 1;
}

int handleConvertDbcBatch(int& i, int argc, char** argv) {
    // Bulk DBC→JSON conversion. Walks <srcDir> recursively for
    // every .dbc file and re-invokes --convert-dbc-json per
    // file. Each .json sidecar is written next to the source.
    // Final commit in the four-format batch-converter set:
    // m2/wmo/blp/dbc → wom/wob/png/json. Run all four to
    // migrate an extracted Data tree end-to-end.
    std::string srcDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "convert-dbc-batch: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    std::vector<std::string> dbcFiles;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext != ".dbc") continue;
        dbcFiles.push_back(e.path().string());
    }
    std::sort(dbcFiles.begin(), dbcFiles.end());
    std::printf("convert-dbc-batch: %s\n", srcDir.c_str());
    std::printf("  candidates : %zu .dbc file(s)\n", dbcFiles.size());
    std::string self = argv[0];
    int ok = 0, failed = 0;
    for (const auto& dbc : dbcFiles) {
        std::fflush(stdout);
        int rc = wowee::editor::cli::runChild(self,
            {"--convert-dbc-json", dbc}, /*quiet=*/true);
        if (rc == 0) {
            ok++;
            std::printf("  [ok]   %s\n", dbc.c_str());
        } else {
            failed++;
            std::printf("  [FAIL] %s (rc=%d)\n", dbc.c_str(), rc);
        }
    }
    std::printf("\n  summary    : %d ok, %d failed (out of %zu)\n",
                ok, failed, dbcFiles.size());
    return failed == 0 ? 0 : 1;
}


}  // namespace

bool handleConvert(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--convert-m2-batch") == 0 && i + 1 < argc) {
        outRc = handleConvertM2Batch(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--convert-wmo-batch") == 0 && i + 1 < argc) {
        outRc = handleConvertWmoBatch(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--convert-blp-batch") == 0 && i + 1 < argc) {
        outRc = handleConvertBlpBatch(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--convert-dbc-batch") == 0 && i + 1 < argc) {
        outRc = handleConvertDbcBatch(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
