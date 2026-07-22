#include "cli_catalog_grep.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace fs = std::filesystem;

struct GrepHit {
    fs::path relPath;
    const FormatMagicEntry* fmt;
    uint32_t version;
    uint32_t entryCount;
    std::string catalogName;
};

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool readStandardHeader(const fs::path& path, char magic[4],
                        uint32_t& version, std::string& catalogName,
                        uint32_t& entryCount) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    if (!is.read(magic, 4) || is.gcount() != 4) return false;
    if (!is.read(reinterpret_cast<char*>(&version), 4)) return false;
    uint32_t nameLen = 0;
    if (!is.read(reinterpret_cast<char*>(&nameLen), 4)) return false;
    if (nameLen > (1u << 20)) return false;
    catalogName.resize(nameLen);
    if (nameLen > 0) {
        if (!is.read(catalogName.data(), nameLen)) return false;
    }
    if (!is.read(reinterpret_cast<char*>(&entryCount), 4)) return false;
    return true;
}

int handleGrep(int& i, int argc, char** argv) {
    std::string pattern = argv[++i];
    if (i + 1 >= argc) {
        std::fprintf(stderr,
            "catalog-grep: missing <dir> argument after pattern\n");
        return 1;
    }
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    bool caseSensitive = false;
    while (i + 1 < argc) {
        std::string a = argv[i + 1];
        if (a == "--case-sensitive") { caseSensitive = true; ++i; }
        else break;
    }
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "catalog-grep: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::string needle = caseSensitive ? pattern : toLower(pattern);
    std::vector<GrepHit> hits;
    uint64_t scanned = 0;
    uint64_t recognized = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        ++scanned;
        char magic[4];
        uint32_t version = 0, entryCount = 0;
        std::string catalogName;
        if (!readStandardHeader(entry.path(), magic, version,
                                 catalogName, entryCount)) {
            continue;
        }
        const FormatMagicEntry* fmt = findFormatByMagic(magic);
        // Only catalog formats have the standard header; world/
        // asset formats (infoFlag == nullptr) won't match.
        if (!fmt || fmt->infoFlag == nullptr) continue;
        ++recognized;
        std::string haystack =
            caseSensitive ? catalogName : toLower(catalogName);
        if (haystack.find(needle) == std::string::npos) continue;
        GrepHit h;
        h.relPath = fs::relative(entry.path(), dir);
        h.fmt = fmt;
        h.version = version;
        h.entryCount = entryCount;
        h.catalogName = catalogName;
        hits.push_back(std::move(h));
    }
    if (jsonOut) {
        nlohmann::json j;
        j["pattern"] = pattern;
        j["caseSensitive"] = caseSensitive;
        j["dir"] = dir;
        j["scanned"] = scanned;
        j["recognized"] = recognized;
        j["matches"] = hits.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& h : hits) {
            char ms[5] = {h.fmt->magic[0], h.fmt->magic[1],
                          h.fmt->magic[2], h.fmt->magic[3], 0};
            arr.push_back({
                {"path", h.relPath.string()},
                {"magic", ms},
                {"extension", h.fmt->extension},
                {"version", h.version},
                {"entryCount", h.entryCount},
                {"catalogName", h.catalogName},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return hits.empty() ? 1 : 0;
    }
    std::printf("catalog-grep: pattern='%s'%s in %s\n",
                 pattern.c_str(),
                 caseSensitive ? " (case-sensitive)" : "",
                 dir.c_str());
    std::printf("  scanned     : %llu\n",
                static_cast<unsigned long long>(scanned));
    std::printf("  recognized  : %llu (catalog headers parsed)\n",
                static_cast<unsigned long long>(recognized));
    std::printf("  matches     : %zu\n", hits.size());
    if (hits.empty()) {
        std::printf("  (no catalog names matched)\n");
        return 1;
    }
    std::printf("\n");
    std::printf("    magic   ext      ver  entries  catalogName              path\n");
    std::printf("    ------  -------  ---  -------  -----------------------  ------\n");
    for (const auto& h : hits) {
        char ms[5] = {h.fmt->magic[0], h.fmt->magic[1],
                      h.fmt->magic[2], h.fmt->magic[3], 0};
        std::printf("    %-6s  %-7s  %3u  %7u  %-23s  %s\n",
                     ms, h.fmt->extension, h.version, h.entryCount,
                     h.catalogName.c_str(), h.relPath.string().c_str());
    }
    return 0;
}

} // namespace

bool handleCatalogGrep(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--catalog-grep") == 0 && i + 2 < argc) {
        outRc = handleGrep(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
