// Where this client's assets live when nobody has said, and what is in there.
//
// Two programs needed the same answer and had neither. The client looked in a
// macOS-only location and nowhere in particular elsewhere; the asset manager
// wrote to Data/ beside whatever directory the terminal happened to be in - so
// running it from a home folder spent eight minutes writing a complete
// extraction the client would never look at, with nothing saying where it went.

#include <catch2/catch_amalgamated.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/data_paths.hpp"

namespace fs = std::filesystem;
using namespace wowee::core;

namespace {

struct Sandbox {
    fs::path root;
    Sandbox() {
        static std::atomic<int> counter{0};
        root = fs::temp_directory_path() /
               ("wowee_paths_" + std::to_string(counter.fetch_add(1)) + "_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(root);
    }
    ~Sandbox() { std::error_code ec; fs::remove_all(root, ec); }

    void extraction(const std::string& expansion) {
        const fs::path at = root / "expansions" / expansion;
        fs::create_directories(at);
        std::ofstream(at / "manifest.json") << "{\"files\":[]}";
    }
    /// A directory with no manifest: a run that was stopped, or somebody's
    /// empty folder.
    void halfDone(const std::string& expansion) {
        fs::create_directories(root / "expansions" / expansion / "world");
    }
};

}  // namespace

TEST_CASE("the default data root is a real per-user location") {
    const fs::path root = userDataRoot();
    REQUIRE_FALSE(root.empty());
    CHECK(root.is_absolute());
    // It is the folder the assets go in, not the folder above it.
    CHECK(root.filename() == "Data");
}

TEST_CASE("nothing is installed in an empty folder") {
    Sandbox box;
    CHECK(installedExpansions(box.root).empty());
    CHECK_FALSE(holdsExtraction(box.root));
}

TEST_CASE("a folder that does not exist answers empty rather than throwing") {
    CHECK(installedExpansions("/no/such/place/at/all").empty());
    CHECK_FALSE(holdsExtraction("/no/such/place/at/all"));
    CHECK(installedExpansions("").empty());
    CHECK_FALSE(holdsExtraction(""));
}

TEST_CASE("every game built into one folder is found") {
    Sandbox box;
    box.extraction("wotlk");
    box.extraction("tbc");
    box.extraction("classic");

    const std::vector<std::string> found = installedExpansions(box.root);
    REQUIRE(found.size() == 3);
    // Sorted, so the same folder reads the same way twice running - a
    // directory iterator's order is not promised to be anything.
    CHECK(found[0] == "classic");
    CHECK(found[1] == "tbc");
    CHECK(found[2] == "wotlk");
    CHECK(holdsExtraction(box.root));
}

TEST_CASE("an extraction that never finished does not count as installed") {
    Sandbox box;
    box.halfDone("wotlk");
    // The manifest is written last. A directory of files with no manifest is a
    // run that stopped partway, and offering it to play with is offering a
    // game with holes in it.
    CHECK(installedExpansions(box.root).empty());
    CHECK_FALSE(holdsExtraction(box.root));

    box.extraction("wotlk");
    CHECK(installedExpansions(box.root).size() == 1);
}

TEST_CASE("a manifest at the root counts, without any expansions under it") {
    Sandbox box;
    std::ofstream(box.root / "manifest.json") << "{\"files\":[]}";
    // The older layout, from before expansions had folders of their own. The
    // client still reads it, so it still counts as something being there.
    CHECK(holdsExtraction(box.root));
    CHECK(installedExpansions(box.root).empty());
}
