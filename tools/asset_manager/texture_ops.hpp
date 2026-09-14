#pragma once

/// Resampling a texture and writing it back as compressed blocks.
///
/// The pass this replaces ran on Pillow and NumPy, which meant an interpreter
/// with two packages compiled into it before a single texture could be touched.
/// Everything here is arithmetic over a byte buffer.
///
/// Alpha is treated as coverage rather than as an image throughout, which is
/// the whole difficulty. A leaf sheet is a silhouette, and a resize that treats
/// its alpha like a colour channel thins the canopy at every scale - so alpha
/// is resized without sharpening and its coverage is put back afterwards, at
/// every mip level, which the GPU's own mip generation cannot do because it has
/// no idea the texture is a cutout.

#include <cstdint>
#include <string>
#include <vector>

namespace wowee::assets {

/// 8-bit RGBA, row-major, tightly packed.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;   ///< width * height * 4

    [[nodiscard]] bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == std::size_t(width) * std::size_t(height) * 4;
    }
};

/// Lanczos-3 resample. Colour and alpha are filtered separately so a sharpen
/// can be applied to one and not the other.
Image resampleLanczos(const Image& src, int width, int height);

/// Unsharp mask over the colour channels only.
///
/// On alpha it would put ringing either side of every leaf edge, which is a
/// halo of holes and a halo of fringe.
void sharpenColour(Image& image, float amount, int radius);

/// Push colour outward into the transparent texels.
///
/// A block-compressed texture stores one colour pair per 4x4 block, so the
/// texels that are fully transparent hold whatever the compressor found
/// convenient - usually near black. Nothing samples them directly, but every
/// filter that touches them does. Filling them with the nearest real colour is
/// the difference between a leaf edge that fades to leaf and one that fades to
/// a dark rim.
void dilateColour(Image& image, int passes = 6, uint8_t threshold = 8);

/// The fraction of texels at or above `cutoff` alpha.
float alphaCoverage(const Image& image, float cutoff);

/// Scale alpha so the same fraction of the sheet survives the cutout.
///
/// Any resampling of an alpha channel moves its coverage; a canopy comes out
/// thinner every time. This searches for the multiplier that restores it.
void restoreCoverage(Image& image, float target, float cutoff);

/// Halve an image without letting alpha weight the colour.
///
/// The obvious way - filtering RGBA together - is premultiplied, and a texel
/// that is fully transparent contributes nothing and comes out black. That is
/// right for a texture whose transparent texels hold junk and wrong for one
/// whose transparent texels were dilated on purpose.
Image halveStraight(const Image& image);

/// Every mip level down to 1x1, with the cutout's coverage held at each.
std::vector<Image> buildMipChain(const Image& base, float cutoff);

/// BC3 (DXT5) blocks for one image. Width and height are rounded up to 4.
std::vector<uint8_t> encodeBC3(const Image& image);

/// A .dds file holding the whole chain as BC3.
std::vector<uint8_t> writeDDS(const std::vector<Image>& levels);

}  // namespace wowee::assets
