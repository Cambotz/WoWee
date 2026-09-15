// Finding the Data folder, whatever somebody points at.
//
// "Choose your World of Warcraft folder" is answered with the folder that has
// Data in it, with Data itself, with the folder everything was unzipped into
// that has the game one level down, and with the download folder where it is
// all still a .zip. Every one of those is a reasonable reading, and the last
// one is a person who needs telling rather than a person who is wrong.
#include <catch_amalgamated.hpp>

#include "install_probe.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using wowee::assets::findDataDir;

namespace {

/// A directory tree to look at, cleaned up afterwards.
struct Sandbox {
    fs::path root;
    explicit Sandbox(const std::string& name) {
        // Named for this run, so two of these at once - which is what ctest
        // does - cannot tidy away each other's directories.
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("wowee_probe_" + name + "_" + std::to_string(now));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void file(const std::string& relative) const {
        const fs::path at = root / relative;
        fs::create_directories(at.parent_path());
        std::ofstream(at) << "not really an archive";
    }
};

/// The archives a WotLK install has, which is what the probe is looking for.
void layDownAGame(const Sandbox& box, const std::string& dataDir) {
    for (const char* name : {"common.MPQ", "common-2.MPQ", "expansion.MPQ",
                             "lichking.MPQ", "patch.MPQ"}) {
        box.file(dataDir + "/" + name);
    }
    box.file(dataDir + "/enUS/locale-enUS.MPQ");
}

}  // namespace

TEST_CASE("the Data folder itself") {
    Sandbox box("data");
    layDownAGame(box, ".");
    const auto probe = findDataDir(box.root.string());
    CHECK(probe.archives >= 3);
    CHECK(fs::path(probe.dataDir) == box.root);
}

TEST_CASE("the folder that has Data inside it") {
    // What the field actually asks for, and what this could not find before.
    Sandbox box("root");
    layDownAGame(box, "Data");
    const auto probe = findDataDir(box.root.string());
    CHECK(probe.archives >= 3);
    CHECK(fs::path(probe.dataDir) == box.root / "Data");
}

TEST_CASE("a Data folder spelled any way at all") {
    // A repack unpacked on a case-sensitive filesystem.
    Sandbox box("lower");
    layDownAGame(box, "data");
    const auto probe = findDataDir(box.root.string());
    CHECK(probe.archives >= 3);
    CHECK(fs::path(probe.dataDir) == box.root / "data");
}

TEST_CASE("the folder everything was unzipped into") {
    // The game one level further down than the field asks for, which is what
    // an archive that carries its own top folder unpacks to.
    Sandbox box("wrapper");
    layDownAGame(box, "World of Warcraft 3.3.5a/Data");
    const auto probe = findDataDir(box.root.string());
    CHECK(probe.archives >= 3);
    CHECK(fs::path(probe.dataDir) == box.root / "World of Warcraft 3.3.5a" / "Data");
}

TEST_CASE("a download that has not been unpacked") {
    // The likeliest reason a folder has no archives in it, and the one a
    // person cannot guess from "no game archives here".
    Sandbox box("zipped");
    box.file("WoW-3.3.5a-enUS.zip");
    box.file("wow-installer.exe");
    const auto probe = findDataDir(box.root.string());
    CHECK(probe.archives == 0);
    CHECK(probe.dataDir.empty());
    CHECK(probe.packed == 2);
}

TEST_CASE("an empty folder is empty, and says how far it looked") {
    Sandbox box("empty");
    box.file("notes/readme.txt");
    const auto probe = findDataDir(box.root.string());
    CHECK(probe.archives == 0);
    CHECK(probe.dataDir.empty());
    CHECK(probe.packed == 0);
    CHECK(probe.searched >= 1);
}

TEST_CASE("the nearest copy wins") {
    // A folder holding both the game and a backup of its Data resolves to the
    // one nearest what was chosen, because that is the one being pointed at.
    Sandbox box("two");
    layDownAGame(box, "Data");
    layDownAGame(box, "backup/old install/Data");
    const auto probe = findDataDir(box.root.string());
    CHECK(fs::path(probe.dataDir) == box.root / "Data");
}

TEST_CASE("a folder that is not there answers nothing rather than guessing") {
    const auto probe = findDataDir((fs::temp_directory_path() / "wowee_no_such_dir").string());
    CHECK(probe.archives == 0);
    CHECK(probe.dataDir.empty());
    CHECK(probe.searched == 0);
    CHECK(findDataDir("").dataDir.empty());
}

TEST_CASE("a stray patch file is not an installation") {
    // One .mpq saved beside a download is remembered but not taken as the
    // answer: a real Data folder has several.
    Sandbox box("stray");
    box.file("patch-3.MPQ");
    layDownAGame(box, "game/Data");
    const auto probe = findDataDir(box.root.string());
    CHECK(fs::path(probe.dataDir) == box.root / "game" / "Data");
}
