// What assets are installed, and whether they can be used.
//
// The client used to find out the hard way. With nothing extracted it started
// anyway, reported no expansions, fell back to a hardcoded default and failed
// somewhere far from the cause - a missing texture, a model that would not
// load, a DBC lookup returning nothing. None of it said there were no assets,
// and none of it said where it had looked.

#include <catch2/catch_amalgamated.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include "pipeline/asset_inventory.hpp"

namespace fs = std::filesystem;
using namespace wowee::pipeline;

namespace {

struct Sandbox {
    fs::path root;
    Sandbox() {
        static std::atomic<int> counter{0};
        root = fs::temp_directory_path() /
               ("wowee_inv_" + std::to_string(counter.fetch_add(1)) + "_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(root);
    }
    ~Sandbox() { std::error_code ec; fs::remove_all(root, ec); }

    fs::path expansion(const std::string& id) { return root / "expansions" / id; }

    /// The directory every expansion has, holding the protocol definitions this
    /// client ships. No art in it.
    void protocolOnly(const std::string& id, const std::string& name) {
        fs::create_directories(expansion(id));
        std::ofstream(expansion(id) / "expansion.json")
            << "{ \"id\": \"" << id << "\", \"name\": \"" << name << "\" }";
    }

    /// A finished extraction: the art directories and the manifest.
    void extracted(const std::string& id, const std::string& name, uint64_t files) {
        protocolOnly(id, name);
        for (const char* dir : {"dbfilesclient", "interface", "world", "character", "creature"}) {
            fs::create_directories(expansion(id) / dir);
        }
        std::ofstream(expansion(id) / "manifest.json")
            << "{\n  \"version\": 1,\n  \"basePath\": \".\",\n  \"fileCount\": " << files
            << ",\n  \"entries\": {}\n}";
    }

    /// Art written but no manifest, which is what a run that was stopped leaves.
    void interrupted(const std::string& id, const std::string& name) {
        extracted(id, name, 10);
        fs::remove(expansion(id) / "manifest.json");
    }

    void sharpened(const std::string& id, int count) {
        fs::create_directories(expansion(id) / "override");
        std::ofstream out(expansion(id) / "override" / "upscale_manifest.json");
        out << "{\n \"entries\": {\n";
        for (int i = 0; i < count; ++i) {
            out << "  \"world/tree/tree" << i << ".dds\": { \"mips\": 11 },\n";
            // The source it read, recorded in the same file. Counting every key
            // would count each texture twice.
            out << "  \"world/tree/tree" << i << ".blp\": { \"source\": true },\n";
        }
        out << " }\n}";
    }

    /// The other shape: one manifest at the data root, covering every
    /// expansion, with keys naming which.
    void sharpenedAtRoot(const std::string& id, int count) {
        std::ofstream out(root / "upscale_manifest.json", std::ios::app);
        for (int i = 0; i < count; ++i) {
            out << "  \"expansions/" << id << "/world/rock" << i << ".dds\": { \"mips\": 9 },\n";
        }
    }

    void imported(const std::string& id, int count) {
        const fs::path at = expansion(id) / "override" / "creature" / "thing";
        fs::create_directories(at);
        for (int i = 0; i < count; ++i) {
            std::ofstream(at / ("thing" + std::to_string(i) + ".m2")) << "MD20";
            // Skins come with them and are not models.
            std::ofstream(at / ("thing" + std::to_string(i) + "00.skin")) << "SKIN";
        }
    }
};

}  // namespace

TEST_CASE("an empty folder has nothing, and says what to do") {
    Sandbox box;
    const AssetInventory inventory = takeInventory(box.root.string());
    CHECK(inventory.sets.empty());
    CHECK_FALSE(inventory.anyUsable());

    const std::string trouble = inventory.troubleText();
    CHECK_FALSE(trouble.empty());
    // Where it looked and what to run: the two things the client never said.
    CHECK(trouble.find(box.root.string()) != std::string::npos);
    CHECK(trouble.find("wowee_assets") != std::string::npos);
}

TEST_CASE("the protocol directories this client ships are not assets") {
    Sandbox box;
    // Every expansion has one of these whether or not anyone has extracted that
    // game, so their presence must not read as "something is installed".
    box.protocolOnly("classic", "Vanilla");
    box.protocolOnly("tbc", "The Burning Crusade");

    const AssetInventory inventory = takeInventory(box.root.string());
    REQUIRE(inventory.sets.size() == 2);
    CHECK_FALSE(inventory.anyUsable());
    for (const AssetSet& set : inventory.sets) {
        CHECK_FALSE(set.anyAssets);
        CHECK_FALSE(set.usable());
        CHECK(set.summary().find("nothing built") != std::string::npos);
    }
    CHECK(inventory.troubleText().find("No game assets") != std::string::npos);
}

TEST_CASE("an extraction that was stopped partway is not offered, and says so") {
    Sandbox box;
    box.interrupted("wotlk", "Wrath of the Lich King");

    const AssetInventory inventory = takeInventory(box.root.string());
    REQUIRE(inventory.sets.size() == 1);
    CHECK(inventory.sets[0].anyAssets);
    // The manifest is written last, so art without one is a run that stopped.
    CHECK_FALSE(inventory.sets[0].complete);
    CHECK_FALSE(inventory.sets[0].usable());
    CHECK(inventory.sets[0].summary().find("did not finish") != std::string::npos);
    CHECK(inventory.troubleText().find("cannot be used") != std::string::npos);
}

TEST_CASE("a finished extraction is usable and counts its files") {
    Sandbox box;
    box.extracted("wotlk", "Wrath of the Lich King", 199468);

    const AssetInventory inventory = takeInventory(box.root.string());
    REQUIRE(inventory.sets.size() == 1);
    const AssetSet& set = inventory.sets[0];
    CHECK(set.id == "wotlk");
    CHECK(set.name == "Wrath of the Lich King");
    CHECK(set.usable());
    // Read out of the head of the manifest, never by parsing it: the real one
    // is thirty megabytes and this runs at startup.
    CHECK(set.fileCount == 199468);
    CHECK(inventory.anyUsable());
    CHECK(inventory.troubleText().empty());
    CHECK(set.summary().find("199,468 files") != std::string::npos);
}

TEST_CASE("the upgrades built on top are counted") {
    Sandbox box;
    box.extracted("wotlk", "Wrath of the Lich King", 1000);
    box.sharpened("wotlk", 54);
    box.imported("wotlk", 7);

    // Held in a variable: find() answers with a pointer into the inventory, and
    // a temporary one dies at the semicolon.
    const AssetInventory inventory = takeInventory(box.root.string());
    const AssetSet* set = inventory.find("wotlk");
    REQUIRE(set != nullptr);
    CHECK(set->sharpenedTextures == 54);
    // Skins arrive with the models and are not models.
    CHECK(set->importedModels == 7);
    CHECK(set->summary().find("54 sharpened textures") != std::string::npos);
    CHECK(set->summary().find("7 imported models") != std::string::npos);
}

TEST_CASE("one game is not credited with another's sharpened textures") {
    Sandbox box;
    box.extracted("wotlk", "Wrath of the Lich King", 1000);
    box.extracted("tbc", "The Burning Crusade", 900);
    // The manifest at the data root covers every expansion at once, so without
    // reading which it names, a second game installed beside the first is
    // credited with all of the first's work.
    box.sharpenedAtRoot("wotlk", 12);
    box.sharpenedAtRoot("tbc", 3);

    const AssetInventory inventory = takeInventory(box.root.string());
    const AssetSet* wotlk = inventory.find("wotlk");
    const AssetSet* tbc = inventory.find("tbc");
    REQUIRE(wotlk != nullptr);
    REQUIRE(tbc != nullptr);
    CHECK(wotlk->sharpenedTextures == 12);
    CHECK(tbc->sharpenedTextures == 3);
}

TEST_CASE("a set missing a directory the client reads is not usable") {
    Sandbox box;
    box.extracted("wotlk", "Wrath of the Lich King", 1000);
    fs::remove_all(box.expansion("wotlk") / "dbfilesclient");

    const AssetInventory inventory = takeInventory(box.root.string());
    const AssetSet* set = inventory.find("wotlk");
    REQUIRE(set != nullptr);
    CHECK(set->complete);          // the manifest is still there
    CHECK_FALSE(set->usable());    // and it still cannot be played
    CHECK(set->summary().find("dbfilesclient") != std::string::npos);
}

TEST_CASE("several games in one folder are all found, and one usable is enough") {
    Sandbox box;
    box.extracted("wotlk", "Wrath of the Lich King", 1000);
    box.extracted("tbc", "The Burning Crusade", 900);
    box.protocolOnly("classic", "Vanilla");

    const AssetInventory inventory = takeInventory(box.root.string());
    REQUIRE(inventory.sets.size() == 3);
    // Sorted, so the same folder reads the same way twice running.
    CHECK(inventory.sets[0].id == "classic");
    CHECK(inventory.sets[1].id == "tbc");
    CHECK(inventory.sets[2].id == "wotlk");
    CHECK(inventory.anyUsable());
    CHECK(inventory.troubleText().empty());
}
