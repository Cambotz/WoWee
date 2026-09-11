#include "pipeline/dds_loader.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cstring>

namespace wowee {
namespace pipeline {

namespace {

constexpr uint32_t kMagic = 0x20534444u;  // "DDS "
constexpr size_t kHeaderBytes = 128;      // magic + 124-byte DDS_HEADER
constexpr size_t kDx10HeaderBytes = 20;   // DDS_HEADER_DXT10, when present

// Offsets into the file, magic included, so they read as file positions.
constexpr size_t kOfsHeaderSize = 4;
constexpr size_t kOfsHeight = 12;
constexpr size_t kOfsWidth = 16;
constexpr size_t kOfsMipCount = 28;
constexpr size_t kOfsPixelFormatFlags = 80;
constexpr size_t kOfsFourCC = 84;
constexpr size_t kOfsDxgiFormat = 128;

constexpr uint32_t kFourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

/// The four-character code as it reads in a log, so a refusal names the format
/// the file actually claims rather than a number.
std::string fourCCText(uint32_t fourCC) {
    std::string out(4, ' ');
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((fourCC >> (i * 8)) & 0xFF);
        out[static_cast<size_t>(i)] = (c >= 32 && c < 127) ? c : '?';
    }
    return out;
}

}  // namespace

size_t DdsLoader::levelBytes(BLPCompression compression, int width, int height) {
    const size_t blocksX = static_cast<size_t>(std::max(1, (width + 3) / 4));
    const size_t blocksY = static_cast<size_t>(std::max(1, (height + 3) / 4));
    const size_t blockSize = (compression == BLPCompression::DXT1) ? 8u : 16u;
    return blocksX * blocksY * blockSize;
}

BLPImage DdsLoader::load(const std::vector<uint8_t>& ddsData) {
    BLPImage image;
    if (ddsData.size() < kHeaderBytes) {
        LOG_WARNING("DDS too small: ", ddsData.size(), " bytes");
        return image;
    }
    if (readU32(ddsData, 0) != kMagic || readU32(ddsData, kOfsHeaderSize) != 124) {
        LOG_WARNING("DDS header not recognised");
        return image;
    }

    const int height = static_cast<int>(readU32(ddsData, kOfsHeight));
    const int width = static_cast<int>(readU32(ddsData, kOfsWidth));
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        LOG_WARNING("DDS dimensions out of range: ", width, "x", height);
        return image;
    }

    constexpr uint32_t kDdpfFourCC = 0x4u;
    if ((readU32(ddsData, kOfsPixelFormatFlags) & kDdpfFourCC) == 0) {
        // An uncompressed DDS has nothing this path wants: the PNG sidecar is
        // the way to hand over RGBA8, and it is already read.
        LOG_WARNING("DDS is not block compressed; use a .png sidecar instead");
        return image;
    }

    const uint32_t fourCC = readU32(ddsData, kOfsFourCC);
    size_t dataOffset = kHeaderBytes;
    BLPCompression compression = BLPCompression::NONE;
    if (fourCC == kFourCC('D', 'X', 'T', '1')) {
        compression = BLPCompression::DXT1;
    } else if (fourCC == kFourCC('D', 'X', 'T', '3')) {
        compression = BLPCompression::DXT3;
    } else if (fourCC == kFourCC('D', 'X', 'T', '5')) {
        compression = BLPCompression::DXT5;
    } else if (fourCC == kFourCC('D', 'X', '1', '0')) {
        // Written by texconv and by Pillow's "BC3" spelling. The extra header
        // carries a DXGI format where the legacy one carried a four-character
        // code; the sRGB variants are the same blocks, and the renderer
        // uploads UNORM either way.
        if (ddsData.size() < kHeaderBytes + kDx10HeaderBytes) {
            LOG_WARNING("DDS claims a DX10 header but is too small to hold one");
            return image;
        }
        dataOffset = kHeaderBytes + kDx10HeaderBytes;
        switch (readU32(ddsData, kOfsDxgiFormat)) {
            case 71: case 72: compression = BLPCompression::DXT1; break;
            case 74: case 75: compression = BLPCompression::DXT3; break;
            case 77: case 78: compression = BLPCompression::DXT5; break;
            default:
                LOG_WARNING("DDS DXGI format ", readU32(ddsData, kOfsDxgiFormat),
                            " is not BC1/BC2/BC3");
                return image;
        }
    } else {
        LOG_WARNING("DDS format '", fourCCText(fourCC), "' is not DXT1/DXT3/DXT5");
        return image;
    }

    const uint32_t declaredMips = readU32(ddsData, kOfsMipCount);
    const uint32_t mipCount = std::max(1u, declaredMips);

    size_t at = dataOffset;
    int w = width;
    int h = height;
    for (uint32_t level = 0; level < mipCount; ++level) {
        const size_t bytes = levelBytes(compression, w, h);
        if (at + bytes > ddsData.size()) {
            // A chain that stops early is still usable as far as it goes; one
            // that has no level zero is not.
            if (level == 0) {
                LOG_WARNING("DDS is shorter than its own level 0 (", width, "x", height, ")");
                return image;
            }
            LOG_WARNING("DDS mip chain ends at level ", level, " of ", mipCount,
                        "; the rest of the file is missing");
            break;
        }
        image.mipmaps.emplace_back(ddsData.begin() + static_cast<long>(at),
                                   ddsData.begin() + static_cast<long>(at + bytes));
        at += bytes;
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }

    image.width = width;
    image.height = height;
    image.channels = 4;
    image.format = BLPFormat::BLP2;
    image.compression = compression;
    image.mipLevels = static_cast<int>(image.mipmaps.size());
    return image;
}

}  // namespace pipeline
}  // namespace wowee
