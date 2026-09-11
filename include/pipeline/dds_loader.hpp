#pragma once

#include "pipeline/blp_loader.hpp"

#include <cstdint>
#include <vector>

namespace wowee {
namespace pipeline {

/**
 * DDS texture loader, for block-compressed sidecars.
 *
 * A texture override written beside a .blp is read in preference to it, and a
 * PNG override arrives as RGBA8: a 1024-square sheet is 5.3 MB on the GPU with
 * its generated mips against 85 KB for the 256-square DXT5 it replaced, which
 * is sixteen times the texels and four times again for the compression thrown
 * away. A .dds sidecar keeps the blocks, so four times the resolution costs
 * sixteen times the memory and not sixty-four.
 *
 * The mip chain comes from the file rather than being generated on upload.
 * That is the point of writing one: a cutout's mips can be built with its
 * coverage preserved, which a box filter on the GPU cannot do.
 *
 * DXT1, DXT3 and DXT5 only - the formats the BLPs themselves use and the
 * renderer already uploads. Anything else is refused by name in the log.
 */
class DdsLoader {
public:
    /// Parse a DDS file into the same shape a block-compressed BLP produces.
    /// Returns an invalid image for anything this does not understand.
    static BLPImage load(const std::vector<uint8_t>& ddsData);

    /// Bytes one mip level of a block-compressed image occupies.
    static size_t levelBytes(BLPCompression compression, int width, int height);
};

}  // namespace pipeline
}  // namespace wowee
