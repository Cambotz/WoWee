// The override directory, for files the expansion never had.
//
// A pack may add as well as replace - tools/asset_pack_curate.py states the
// rule and tools/asset_pack_from_client.py builds packs that rely on it. A
// replacement resolves through the manifest entry it supersedes, but an
// addition has no entry to resolve against: Cataclysm's leaf atlas arrives
// under a name 3.3.5 never had. Resolution used to stop there, so a model
// dropped in as an override rendered white, its own new textures unreachable
// while it was not.
//
// AssetManager cannot be built here - it wants an extracted tree and a 32 MB
// manifest - so this pins the resolution order itself, in the shape
// resolveFile implements it.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "pipeline/asset_manifest.hpp"

using namespace wowee::pipeline;

namespace {

namespace fs = std::filesystem;

struct TempTree {
    fs::path root;
    TempTree() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() /
               ("wowee_override_test_" + std::to_string(stamp));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempTree() { fs::remove_all(root); }

    void write(const std::string& rel, const std::string& contents) const {
        fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << contents;
    }
};

/// resolveFile's order, as src/pipeline/asset_manager.cpp implements it:
/// the override's manifest-resolved path, then the override taken as a path,
/// then the manifest, then the loose file.
std::string resolveLikeAssetManager(const fs::path& dataPath,
                                    const AssetManifest& manifest,
                                    const std::string& normalizedPath) {
    const std::string overridePath = (dataPath / "override").string();

    if (const auto* entry = manifest.lookup(normalizedPath)) {
        if (!entry->filesystemPath.empty()) {
            std::string candidate = overridePath + "/" + entry->filesystemPath;
            if (fs::exists(candidate)) return candidate;
        }
    }
    std::string byPath = normalizedPath;
    std::replace(byPath.begin(), byPath.end(), '\\', '/');
    if (fs::exists(overridePath + "/" + byPath)) return overridePath + "/" + byPath;

    std::string primary = manifest.resolveFilesystemPath(normalizedPath);
    if (!primary.empty()) return primary;

    std::string loose = normalizedPath;
    std::replace(loose.begin(), loose.end(), '\\', '/');
    loose = (dataPath / loose).string();
    if (fs::exists(loose)) return loose;
    return {};
}

/// A two-entry manifest over the temp tree.
AssetManifest manifestOver(const TempTree& tree) {
    tree.write("manifest.json", R"({
      "version": 1,
      "basePath": ".",
      "fileCount": 1,
      "entries": {
        "world\\trees\\old.blp": {"p": "world/trees/old.blp", "s": 3, "h": "00000000"}
      }
    })");
    AssetManifest manifest;
    REQUIRE(manifest.load((tree.root / "manifest.json").string()));
    return manifest;
}

}  // namespace

TEST_CASE("an override replaces a file the manifest knows") {
    TempTree tree;
    AssetManifest manifest = manifestOver(tree);
    tree.write("world/trees/old.blp", "base");
    tree.write("override/world/trees/old.blp", "pack");

    const std::string resolved =
        resolveLikeAssetManager(tree.root, manifest, "world\\trees\\old.blp");
    REQUIRE_FALSE(resolved.empty());
    CHECK(resolved.find("/override/") != std::string::npos);
}

TEST_CASE("an override adds a file the manifest has never heard of") {
    TempTree tree;
    AssetManifest manifest = manifestOver(tree);
    // No manifest entry, no file in the tree - only the pack has it.
    tree.write("override/world/trees/leaves_set.blp", "pack");

    const std::string resolved =
        resolveLikeAssetManager(tree.root, manifest, "world\\trees\\leaves_set.blp");
    REQUIRE_FALSE(resolved.empty());
    CHECK(resolved.find("/override/") != std::string::npos);
}

TEST_CASE("without the override, the manifest still answers") {
    TempTree tree;
    AssetManifest manifest = manifestOver(tree);
    tree.write("world/trees/old.blp", "base");

    const std::string resolved =
        resolveLikeAssetManager(tree.root, manifest, "world\\trees\\old.blp");
    REQUIRE_FALSE(resolved.empty());
    CHECK(resolved.find("/override/") == std::string::npos);
}

TEST_CASE("a path nothing has resolves to nothing") {
    TempTree tree;
    AssetManifest manifest = manifestOver(tree);
    CHECK(resolveLikeAssetManager(tree.root, manifest, "world\\trees\\absent.blp").empty());
}
