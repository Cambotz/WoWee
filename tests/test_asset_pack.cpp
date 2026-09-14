// Writing an asset set out as one file, and installing one somebody sent you.
//
// The refusal is the part worth pinning. A zip entry's name is a string chosen
// by whoever built the archive, and "../../.ssh/authorized_keys" is a valid
// one - so a pack is a file from a stranger that names where its contents land.

#include <catch2/catch_amalgamated.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pack.hpp"

namespace fs = std::filesystem;
using namespace wowee::assets;

namespace {

const std::atomic<bool> kNeverCancelled{false};

struct Sandbox {
    fs::path root;
    Sandbox() {
        static std::atomic<int> counter{0};
        root = fs::temp_directory_path() /
               ("wowee_pack_" + std::to_string(counter.fetch_add(1)) + "_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(root);
    }
    ~Sandbox() { std::error_code ec; fs::remove_all(root, ec); }

    void write(const std::string& relative, const std::string& contents) {
        const fs::path at = root / relative;
        fs::create_directories(at.parent_path());
        std::ofstream out(at, std::ios::binary);
        out << contents;
    }
    [[nodiscard]] std::string read(const std::string& relative) const {
        std::ifstream in(root / relative, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }
    [[nodiscard]] bool has(const std::string& relative) const {
        return fs::exists(root / relative);
    }
};

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(uint8_t(value & 0xFF));
    out.push_back(uint8_t(value >> 8));
}
void put32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t((value >> (i * 8)) & 0xFF));
}

/// A zip holding one stored (uncompressed) entry under whatever name is given,
/// so a name no honest writer would produce can still be handed to the reader.
void writeZipNamed(const fs::path& at, const std::string& entryName,
                   const std::string& contents) {
    std::vector<uint8_t> zip;
    const uint32_t crc = 0;   // the reader does not verify it
    const uint32_t size = uint32_t(contents.size());

    const uint32_t localAt = 0;
    put32(zip, 0x04034B50);
    put16(zip, 20); put16(zip, 0); put16(zip, 0);   // stored
    put16(zip, 0); put16(zip, 0x21);
    put32(zip, crc); put32(zip, size); put32(zip, size);
    put16(zip, uint16_t(entryName.size())); put16(zip, 0);
    zip.insert(zip.end(), entryName.begin(), entryName.end());
    zip.insert(zip.end(), contents.begin(), contents.end());

    const uint32_t directoryAt = uint32_t(zip.size());
    put32(zip, 0x02014B50);
    put16(zip, 20); put16(zip, 20); put16(zip, 0); put16(zip, 0);
    put16(zip, 0); put16(zip, 0x21);
    put32(zip, crc); put32(zip, size); put32(zip, size);
    put16(zip, uint16_t(entryName.size()));
    put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0);
    put32(zip, 0); put32(zip, localAt);
    zip.insert(zip.end(), entryName.begin(), entryName.end());

    const uint32_t directorySize = uint32_t(zip.size()) - directoryAt;
    put32(zip, 0x06054B50);
    put16(zip, 0); put16(zip, 0); put16(zip, 1); put16(zip, 1);
    put32(zip, directorySize); put32(zip, directoryAt); put16(zip, 0);

    std::ofstream out(at, std::ios::binary);
    out.write(reinterpret_cast<const char*>(zip.data()), std::streamsize(zip.size()));
}

}  // namespace

TEST_CASE("a pack written here installs back to the same files") {
    Sandbox built;
    built.write("creature/murloc/murloc.m2", "a model");
    built.write("world/tree/tree.blp", std::string(4096, 'x'));
    built.write("db/spell.csv", "id,name\n1,Fireball\n");

    const fs::path zip = built.root / "pack.zip";
    const PackResult wrote = writePack(built.root.string(), zip.string(), "wotlk",
                                       nullptr, kNeverCancelled);
    REQUIRE(wrote.ok);
    CHECK(wrote.files >= 3);

    const PackInfo info = readPackInfo(zip.string());
    REQUIRE(info.ok);
    CHECK(info.name == "wotlk");
    CHECK(info.files >= 3);

    Sandbox installed;
    const PackResult read = readPack(zip.string(), installed.root.string(),
                                     nullptr, kNeverCancelled);
    REQUIRE(read.ok);
    CHECK(read.files >= 3);

    // The Data/ prefix the pack is written under is stripped on the way back
    // out, so a pack installs to the tree it was built from rather than to one
    // nested a directory deeper each time it goes round.
    CHECK(installed.read("creature/murloc/murloc.m2") == "a model");
    CHECK(installed.read("db/spell.csv") == "id,name\n1,Fireball\n");
    CHECK(installed.read("world/tree/tree.blp").size() == 4096);
    CHECK_FALSE(installed.has("Data"));
    CHECK_FALSE(installed.has("pack.json"));
}

TEST_CASE("a pack naming a path outside the destination installs nothing") {
    Sandbox box;
    const fs::path zip = box.root / "hostile.zip";
    writeZipNamed(zip, "Data/../../escaped.txt", "should not be written");

    Sandbox into;
    const PackResult read = readPack(zip.string(), into.root.string(),
                                     nullptr, kNeverCancelled);
    CHECK_FALSE(read.ok);
    CHECK(read.files == 0);
    CHECK_FALSE(fs::exists(into.root.parent_path() / "escaped.txt"));
    CHECK_FALSE(into.has("escaped.txt"));
}

TEST_CASE("an absolute path in a pack is refused too") {
    Sandbox box;
    Sandbox into;

    // Somewhere the test can actually write. Named against a directory with no
    // permission to write it, this passes whether or not the reader refuses
    // anything - which is how it passed while the reader did not: iterating
    // "/etc/passwd" yields "/" as its first component, and appending that to a
    // relative path replaces it rather than extending it, so the assembled
    // path came out absolute and landed exactly where the name asked.
    const fs::path escaped = into.root.parent_path() / "wowee_absolute_escape.txt";
    fs::remove(escaped);

    const fs::path zip = box.root / "absolute.zip";
    writeZipNamed(zip, escaped.string(), "this should not be written");

    const PackResult read = readPack(zip.string(), into.root.string(),
                                     nullptr, kNeverCancelled);
    CHECK_FALSE(read.ok);
    CHECK(read.files == 0);
    CHECK_FALSE(fs::exists(escaped));
}

TEST_CASE("a pack cannot climb out by way of a name that normalises back in") {
    Sandbox box;
    Sandbox into;
    const fs::path escaped = into.root.parent_path() / "wowee_climb_escape.txt";
    fs::remove(escaped);

    const fs::path zip = box.root / "climb.zip";
    writeZipNamed(zip, "Data/world/../../../wowee_climb_escape.txt", "no");

    const PackResult read = readPack(zip.string(), into.root.string(),
                                     nullptr, kNeverCancelled);
    CHECK_FALSE(read.ok);
    CHECK_FALSE(fs::exists(escaped));
}

TEST_CASE("something that is not a zip is refused by name, not by crashing") {
    Sandbox box;
    box.write("notapack.zip", "this is just some text");

    const PackInfo info = readPackInfo((box.root / "notapack.zip").string());
    CHECK_FALSE(info.ok);
    CHECK_FALSE(info.error.empty());
}
