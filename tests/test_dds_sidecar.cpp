// The .dds sidecar the upscale pass writes, read the way AssetManager reads it.
//
// A PNG override arrives as RGBA8 and the renderer generates its mips: four
// times the video memory of the blocks it replaced, and a mip chain that knows
// nothing about the cutout it is filtering. The .dds path hands over DXT
// blocks and the chain as the tool built them, coverage and all.
//
// Pinned here because a header read wrongly is not an error - it is a texture
// that loads at the wrong size, or a chain that runs off the end of the file.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include "pipeline/dds_loader.hpp"

using namespace wowee::pipeline;

namespace {

/// A legacy DDS header, the shape Pillow and texconv both write.
struct DdsBuilder {
    std::vector<uint8_t> bytes;

    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }
    void fourCC(const char* cc) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(cc[i]));
    }

    /// magic, 124-byte header, pixel format, caps - 128 bytes in all.
    void header(uint32_t width, uint32_t height, uint32_t mipCount, const char* cc,
                uint32_t pixelFormatFlags = 0x4) {
        fourCC("DDS ");
        u32(124);                                   // dwSize
        u32(0x1 | 0x2 | 0x4 | 0x1000 | 0x20000);    // caps|height|width|pixelformat|mipcount
        u32(height);
        u32(width);
        u32(0);                                     // dwPitchOrLinearSize
        u32(0);                                     // dwDepth
        u32(mipCount);
        for (int i = 0; i < 11; ++i) u32(0);        // dwReserved1
        u32(32);                                    // ddspf.dwSize
        u32(pixelFormatFlags);                      // DDPF_FOURCC
        fourCC(cc);
        for (int i = 0; i < 5; ++i) u32(0);         // bit count + masks
        u32(0x1000 | 0x400000 | 0x8);               // texture|mipmap|complex
        for (int i = 0; i < 4; ++i) u32(0);         // caps2..4, reserved2
    }

    /// Every level of a chain, filled with a recognisable byte per level.
    void chain(BLPCompression compression, int width, int height, int levels) {
        for (int level = 0; level < levels; ++level) {
            const size_t bytesInLevel = DdsLoader::levelBytes(
                compression, std::max(1, width >> level), std::max(1, height >> level));
            bytes.insert(bytes.end(), bytesInLevel, static_cast<uint8_t>(0xA0 + level));
        }
    }
};

}  // namespace

TEST_CASE("a DXT5 sidecar loads with its whole mip chain") {
    DdsBuilder b;
    b.header(64, 64, 7, "DXT5");
    b.chain(BLPCompression::DXT5, 64, 64, 7);

    const BLPImage image = DdsLoader::load(b.bytes);
    REQUIRE(image.isValid());
    CHECK(image.width == 64);
    CHECK(image.height == 64);
    CHECK(image.compression == BLPCompression::DXT5);
    CHECK(image.isBlockCompressed());
    REQUIRE(image.mipmaps.size() == 7);
    CHECK(image.mipLevels == 7);
    // 64x64 DXT5 is 16x16 blocks of sixteen bytes; each level a quarter of the
    // last, and the last three levels one block each.
    CHECK(image.mipmaps[0].size() == 4096u);
    CHECK(image.mipmaps[1].size() == 1024u);
    CHECK(image.mipmaps[6].size() == 16u);
    // Levels are handed over in order, not all pointed at level zero.
    CHECK(image.mipmaps[0][0] == 0xA0);
    CHECK(image.mipmaps[3][0] == 0xA3);
}

TEST_CASE("DXT1 levels are half the size of DXT5 ones") {
    DdsBuilder b;
    b.header(16, 16, 1, "DXT1");
    b.chain(BLPCompression::DXT1, 16, 16, 1);

    const BLPImage image = DdsLoader::load(b.bytes);
    REQUIRE(image.isValid());
    CHECK(image.compression == BLPCompression::DXT1);
    CHECK(image.mipmaps[0].size() == 128u);  // 4x4 blocks of eight bytes
}

TEST_CASE("a DX10 header carrying BC3 is read as DXT5") {
    DdsBuilder b;
    b.header(8, 8, 1, "DX10");
    b.u32(77);   // DXGI_FORMAT_BC3_UNORM
    b.u32(3);    // resource dimension: texture2d
    b.u32(0);    // misc flag
    b.u32(1);    // array size
    b.u32(0);    // misc flags 2
    b.chain(BLPCompression::DXT5, 8, 8, 1);

    const BLPImage image = DdsLoader::load(b.bytes);
    REQUIRE(image.isValid());
    CHECK(image.compression == BLPCompression::DXT5);
    CHECK(image.mipmaps[0].size() == 64u);
}

TEST_CASE("a chain that stops early keeps the levels that are there") {
    DdsBuilder b;
    b.header(64, 64, 7, "DXT5");
    b.chain(BLPCompression::DXT5, 64, 64, 3);  // three levels, header claims seven

    const BLPImage image = DdsLoader::load(b.bytes);
    REQUIRE(image.isValid());
    CHECK(image.mipmaps.size() == 3);
    CHECK(image.mipLevels == 3);
}

TEST_CASE("what the loader refuses") {
    SECTION("a file that is not a DDS") {
        std::vector<uint8_t> notDds(256, 0x11);
        CHECK_FALSE(DdsLoader::load(notDds).isValid());
    }
    SECTION("a format that is not BC1/BC2/BC3") {
        DdsBuilder b;
        b.header(16, 16, 1, "ATI2");
        b.chain(BLPCompression::DXT5, 16, 16, 1);
        CHECK_FALSE(DdsLoader::load(b.bytes).isValid());
    }
    SECTION("an uncompressed DDS, which is the PNG sidecar's job") {
        DdsBuilder b;
        b.header(16, 16, 1, "DXT5", /*pixelFormatFlags=*/0x41);  // DDPF_RGB|ALPHAPIXELS
        b.chain(BLPCompression::DXT5, 16, 16, 1);
        CHECK_FALSE(DdsLoader::load(b.bytes).isValid());
    }
    SECTION("a header with no level zero behind it") {
        DdsBuilder b;
        b.header(64, 64, 7, "DXT5");
        b.bytes.insert(b.bytes.end(), 100, 0x00);  // short of one 4096-byte level
        CHECK_FALSE(DdsLoader::load(b.bytes).isValid());
    }
    SECTION("dimensions past what any WoW texture is") {
        DdsBuilder b;
        b.header(16384, 16384, 1, "DXT5");
        CHECK_FALSE(DdsLoader::load(b.bytes).isValid());
    }
}

TEST_CASE("level sizes round up to whole blocks") {
    CHECK(DdsLoader::levelBytes(BLPCompression::DXT5, 1, 1) == 16u);
    CHECK(DdsLoader::levelBytes(BLPCompression::DXT5, 2, 2) == 16u);
    CHECK(DdsLoader::levelBytes(BLPCompression::DXT5, 5, 5) == 64u);   // 2x2 blocks
    CHECK(DdsLoader::levelBytes(BLPCompression::DXT1, 1, 1) == 8u);
    CHECK(DdsLoader::levelBytes(BLPCompression::DXT3, 8, 4) == 32u);
}
