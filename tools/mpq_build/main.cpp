/**
 * mpq_build - pack a directory of loose game files into an MPQ patch archive.
 *
 *     mpq_build --input <dir> --output Patch-W.MPQ [--name-prefix <path>]
 *
 * The reverse of asset_extract. WoWee itself reads loose files and never opens
 * a patch archive, so this is not for WoWee: it is for everything else that
 * does. An override directory built here - a zone's trees swapped for a later
 * client's, a model pack curated down to the parts that work - is a directory,
 * and a directory is not something you can hand to a 3.3.5 client, back up as
 * one file with its paths intact, or put beside the other Patch-*.MPQ files a
 * server's players already install.
 *
 * Every file under the input directory goes in under its path relative to that
 * directory, with forward slashes turned into the backslashes MPQ names use.
 * A (listfile) is written, because an archive without one is an archive whose
 * contents can only be found by guessing their names.
 *
 * Archive version 1 by default. That is what a 3.3.5a client reads, and the
 * later versions exist for sizes and file counts an override directory does
 * not reach.
 */

#include <StormLib.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void printUsage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " --input <dir> --output <file.MPQ> [options]\n\n"
        << "Pack a directory of loose game files into an MPQ patch archive.\n\n"
        << "Required:\n"
        << "  --input <dir>       Directory to pack; paths inside it become MPQ names\n"
        << "  --output <file>     Archive to write (overwritten if it exists)\n\n"
        << "Options:\n"
        << "  --name-prefix <p>   Prefix every name inside the archive with this\n"
        << "  --version <1|2>     MPQ format version (default 1, which 3.3.5a reads)\n"
        << "  --no-compress       Store files rather than deflating them\n"
        << "  --quiet             Only report the totals\n";
}

/// MPQ names are backslashed and the client lowercases before it hashes, so
/// either case works; lowercase is what the extractor writes and what a
/// listfile is easiest to read in.
std::string toArchiveName(const fs::path& relative, const std::string& prefix) {
    std::string name = relative.generic_string();
    std::replace(name.begin(), name.end(), '/', '\\');
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (prefix.empty()) return name;
    std::string head = prefix;
    std::replace(head.begin(), head.end(), '/', '\\');
    if (!head.empty() && head.back() != '\\') head.push_back('\\');
    return head + name;
}

/// Files the packer has no business carrying into an archive: macOS writes the
/// first two beside anything it copies to a foreign filesystem, and the third
/// is this project's own note to itself.
bool isNotGameData(const fs::path& path) {
    const std::string name = path.filename().string();
    return name.rfind("._", 0) == 0 || name == ".DS_Store";
}

}  // namespace

int main(int argc, char** argv) {
    fs::path inputDir;
    fs::path outputFile;
    std::string namePrefix;
    int version = 1;
    bool compress = true;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            inputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (std::strcmp(argv[i], "--name-prefix") == 0 && i + 1 < argc) {
            namePrefix = argv[++i];
        } else if (std::strcmp(argv[i], "--version") == 0 && i + 1 < argc) {
            version = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-compress") == 0) {
            compress = false;
        } else if (std::strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputDir.empty() || outputFile.empty()) {
        std::cerr << "Error: --input and --output are required\n\n";
        printUsage(argv[0]);
        return 1;
    }
    std::error_code ec;
    if (!fs::is_directory(inputDir, ec)) {
        std::cerr << "Error: not a directory: " << inputDir << "\n";
        return 1;
    }
    if (version != 1 && version != 2) {
        std::cerr << "Error: --version must be 1 or 2\n";
        return 1;
    }

    // Enumerate first: the archive's hash table is sized at creation and
    // cannot grow, so the count has to be known before anything is added.
    std::vector<fs::path> files;
    uint64_t totalBytes = 0;
    for (const auto& entry : fs::recursive_directory_iterator(inputDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (isNotGameData(entry.path())) continue;
        files.push_back(entry.path());
        totalBytes += static_cast<uint64_t>(entry.file_size(ec));
    }
    if (files.empty()) {
        std::cerr << "Error: no files under " << inputDir << "\n";
        return 1;
    }
    std::sort(files.begin(), files.end());

    // Room to spare, and a power of two: StormLib rounds up, and an archive
    // sized exactly to its contents leaves no room for the listfile and
    // attributes it writes itself.
    DWORD maxFileCount = 16;
    while (maxFileCount < files.size() + 16) maxFileCount *= 2;

    fs::remove(outputFile, ec);
    fs::create_directories(fs::absolute(outputFile).parent_path(), ec);

    const DWORD createFlags =
        (version == 2 ? MPQ_CREATE_ARCHIVE_V2 : MPQ_CREATE_ARCHIVE_V1) | MPQ_CREATE_LISTFILE;

    HANDLE archive = nullptr;
    if (!SFileCreateArchive(outputFile.string().c_str(), createFlags, maxFileCount, &archive)) {
        std::cerr << "Error: could not create " << outputFile << " (StormLib error "
                  << SErrGetLastError() << ")\n";
        return 1;
    }

    if (!quiet) {
        std::cout << "Packing " << files.size() << " file(s), "
                  << (totalBytes / (1024 * 1024)) << " MB\n"
                  << "  into: " << outputFile.string() << "\n"
                  << "  MPQ v" << version << ", " << (compress ? "deflated" : "stored")
                  << ", capacity " << maxFileCount << "\n";
    }

    const DWORD addFlags = MPQ_FILE_REPLACEEXISTING | (compress ? MPQ_FILE_COMPRESS : 0u);
    size_t added = 0;
    size_t failed = 0;
    for (const auto& file : files) {
        const std::string name = toArchiveName(fs::relative(file, inputDir, ec), namePrefix);
        if (!SFileAddFileEx(archive, file.string().c_str(), name.c_str(), addFlags,
                            MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_NEXT_SAME)) {
            std::cerr << "  FAILED " << name << " (StormLib error " << SErrGetLastError() << ")\n";
            ++failed;
            continue;
        }
        ++added;
        if (!quiet && added % 500 == 0) {
            std::cout << "  " << added << " / " << files.size() << "\n" << std::flush;
        }
    }

    SFileCloseArchive(archive);

    const uint64_t archiveBytes = static_cast<uint64_t>(fs::file_size(outputFile, ec));
    std::cout << "Wrote " << outputFile.string() << ": " << added << " file(s)";
    if (failed) std::cout << ", " << failed << " failed";
    std::cout << ", " << (archiveBytes / (1024 * 1024)) << " MB\n";
    return failed ? 1 : 0;
}
