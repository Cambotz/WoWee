// Telling a silhouette from an atlas leftover.
//
// A batch whose material says blend mode 0 does not read its texture's alpha,
// and M2 textures are routinely atlases carrying alpha that belongs to another
// layer or another model. Honouring it anyway puts holes in surfaces that were
// painted solid - Silverpine's trees lost the middle of their trunk to exactly
// that, leaving the canopy floating above the stump.
//
// So BLPImage::alphaIsSilhouette asks what lies under the transparent texels.
// A card drawn on a black backing leaves that backing; an atlas painted edge
// to edge leaves the art. The two populations are far apart on real assets -
// Alterac's thorn cards sit near 6 of 255, Silverpine bark, Zangarmarsh caps
// and Nagrand trunks above 79 - but far apart on real assets is not the same
// as a rule that holds, so this builds both cases by hand and then checks the
// block reader against the decoder over whatever files are present.
#include <catch_amalgamated.hpp>

#include "pipeline/blp_loader.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using wowee::pipeline::BLPCompression;
using wowee::pipeline::BLPImage;
using wowee::pipeline::BLPLoader;

namespace {

/// RGB565, the form both endpoints of a DXT colour block are stored in.
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255));
}

/// The eight colour bytes: two endpoints, then two bits per texel all
/// selecting endpoint 0, so the whole block is one colour.
void appendColorBlock(std::vector<uint8_t>& out, uint16_t c0, uint16_t c1) {
    out.push_back(static_cast<uint8_t>(c0 & 0xFF));
    out.push_back(static_cast<uint8_t>(c0 >> 8));
    out.push_back(static_cast<uint8_t>(c1 & 0xFF));
    out.push_back(static_cast<uint8_t>(c1 >> 8));
    out.insert(out.end(), {0, 0, 0, 0});
}

/// A DXT5 block of one colour at one alpha, built from the endpoints so the
/// interpolants never come into it: both alpha endpoints equal, every index 0.
std::vector<uint8_t> dxt5Block(uint8_t alpha, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> block{alpha, alpha, 0, 0, 0, 0, 0, 0};
    appendColorBlock(block, rgb565(r, g, b), rgb565(r, g, b));
    return block;
}

/// Four blocks - 64 texels, the fewest the scan will draw a conclusion from.
BLPImage dxt5Image(const std::vector<uint8_t>& block, int blocks = 4) {
    BLPImage image;
    image.width = 8;
    image.height = 8;
    image.compression = BLPCompression::DXT5;
    std::vector<uint8_t> level;
    for (int i = 0; i < blocks; ++i) level.insert(level.end(), block.begin(), block.end());
    image.mipmaps.push_back(std::move(level));
    return image;
}

}  // namespace

TEST_CASE("alpha over painted art is not a silhouette", "[blp]") {
    // Silverpine bark: what the trunk's own triangles sample under the stale
    // alpha hole, measured at RGB 107/84/47.
    const BLPImage bark = dxt5Image(dxt5Block(0, 107, 84, 47));
    REQUIRE(bark.hasTransparency());
    CHECK_FALSE(bark.alphaIsSilhouette());
}

TEST_CASE("alpha over a black backing is a silhouette", "[blp]") {
    // Alterac's thorn cards, measured at RGB 7/6/4.
    const BLPImage cards = dxt5Image(dxt5Block(0, 7, 6, 4));
    REQUIRE(cards.hasTransparency());
    CHECK(cards.alphaIsSilhouette());
}

TEST_CASE("an opaque texture has no silhouette to honour", "[blp]") {
    const BLPImage solid = dxt5Image(dxt5Block(255, 107, 84, 47));
    REQUIRE_FALSE(solid.hasTransparency());
    CHECK_FALSE(solid.alphaIsSilhouette());
}

TEST_CASE("DXT1 punch-through is always the silhouette", "[blp]") {
    // c0 <= c1 is the punch-through mode flag, and every texel selects index
    // 3. There is no colour stored under a punched texel to measure, which is
    // the whole reason the format's transparency cannot be second-guessed.
    BLPImage image;
    image.width = 8;
    image.height = 8;
    image.compression = BLPCompression::DXT1;
    std::vector<uint8_t> level;
    for (int i = 0; i < 4; ++i) {
        const uint16_t c0 = 0;
        const uint16_t c1 = 1;
        level.insert(level.end(), {static_cast<uint8_t>(c0 & 0xFF), static_cast<uint8_t>(c0 >> 8),
                                   static_cast<uint8_t>(c1 & 0xFF), static_cast<uint8_t>(c1 >> 8),
                                   0xFF, 0xFF, 0xFF, 0xFF});
    }
    image.mipmaps.push_back(std::move(level));
    REQUIRE(image.hasTransparency());
    CHECK(image.alphaIsSilhouette());
}

TEST_CASE("too few transparent texels decide nothing", "[blp]") {
    // One block of black under alpha, three opaque. Sixteen texels is a corner
    // of one 4x4 block; a verdict drawn from that turns on the encoder's
    // rounding rather than on what the artist drew.
    BLPImage image;
    image.width = 8;
    image.height = 8;
    image.compression = BLPCompression::DXT5;
    std::vector<uint8_t> level;
    const std::vector<uint8_t> clear = dxt5Block(0, 0, 0, 0);
    const std::vector<uint8_t> opaque = dxt5Block(255, 107, 84, 47);
    level.insert(level.end(), clear.begin(), clear.end());
    for (int i = 0; i < 3; ++i) level.insert(level.end(), opaque.begin(), opaque.end());
    image.mipmaps.push_back(std::move(level));
    REQUIRE(image.hasTransparency());
    CHECK_FALSE(image.alphaIsSilhouette());
}

TEST_CASE("the decoded backing scan agrees with the block one", "[blp]") {
    // The same property test_blp_alpha_scan makes of hasTransparency, for the
    // same reason: the block reader rebuilds by hand what the decoder writes,
    // and the only way to know it rebuilds it right is to ask both over real
    // files rather than over the cases someone thought to write down.
#ifdef WOWEE_SOURCE_DIR
    const std::filesystem::path dataDir = std::filesystem::path(WOWEE_SOURCE_DIR) / "Data";
#else
    const std::filesystem::path dataDir = "Data";
#endif
    std::error_code ec;
    if (!std::filesystem::is_directory(dataDir, ec)) {
        WARN("no Data directory - nothing to compare against");
        return;
    }

    int compared = 0;
    int disagreed = 0;
    int silhouettes = 0;
    std::vector<std::string> examples;

    constexpr int kMaxFiles = 400;
    for (std::filesystem::recursive_directory_iterator it(dataDir, ec), end;
         it != end && compared < kMaxFiles; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".blp") continue;

        std::ifstream in(it->path(), std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>()};
        if (bytes.empty()) continue;

        const BLPImage decoded = BLPLoader::load(bytes, false);
        const BLPImage blocks = BLPLoader::load(bytes, true);
        if (!decoded.isValid() || !blocks.isValid()) continue;
        if (!blocks.isBlockCompressed()) continue;

        ++compared;
        if (blocks.alphaIsSilhouette()) ++silhouettes;
        if (decoded.alphaIsSilhouette() != blocks.alphaIsSilhouette()) {
            ++disagreed;
            if (examples.size() < 5) {
                examples.push_back(it->path().filename().string() + ": decoded=" +
                                   (decoded.alphaIsSilhouette() ? "yes" : "no") + " blocks=" +
                                   (blocks.alphaIsSilhouette() ? "yes" : "no"));
            }
        }
    }

    if (compared == 0) {
        WARN("no block-compressed BLP files found - nothing compared");
        return;
    }

    INFO("compared " << compared << " textures, " << disagreed << " disagreed, " << silhouettes
                     << " judged silhouettes");
    for (const auto& e : examples) INFO("  " << e);
    CHECK(disagreed == 0);
}
