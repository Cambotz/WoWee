#include "texture_ops.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wowee::assets {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float sinc(float x) {
    if (std::fabs(x) < 1e-6f) return 1.0f;
    const float px = kPi * x;
    return std::sin(px) / px;
}

/// Lanczos-3: the window most image tools mean by "Lanczos".
float lanczos(float x) {
    constexpr float a = 3.0f;
    if (std::fabs(x) >= a) return 0.0f;
    return sinc(x) * sinc(x / a);
}

uint8_t clampByte(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

/// One separable pass. `horizontal` says which axis is being resampled.
void resamplePass(const Image& src, Image& dst, bool horizontal) {
    const int srcLen = horizontal ? src.width : src.height;
    const int dstLen = horizontal ? dst.width : dst.height;
    const float scale = static_cast<float>(srcLen) / static_cast<float>(dstLen);
    // Downscaling widens the filter so it still averages the texels it is
    // standing in for; upscaling keeps it at its natural width.
    const float support = std::max(1.0f, scale) * 3.0f;

    const int rows = horizontal ? dst.height : dst.width;
    for (int row = 0; row < rows; ++row) {
        for (int out = 0; out < dstLen; ++out) {
            const float centre = (out + 0.5f) * scale - 0.5f;
            const int first = static_cast<int>(std::floor(centre - support));
            const int last = static_cast<int>(std::ceil(centre + support));

            float sum[4] = {0, 0, 0, 0};
            float weightSum = 0.0f;
            for (int tap = first; tap <= last; ++tap) {
                const int clamped = std::clamp(tap, 0, srcLen - 1);
                const float weight = lanczos((tap - centre) / std::max(1.0f, scale));
                if (weight == 0.0f) continue;
                const int x = horizontal ? clamped : row;
                const int y = horizontal ? row : clamped;
                const uint8_t* texel = &src.pixels[(std::size_t(y) * src.width + x) * 4];
                for (int c = 0; c < 4; ++c) sum[c] += weight * texel[c];
                weightSum += weight;
            }
            if (weightSum == 0.0f) weightSum = 1.0f;

            const int x = horizontal ? out : row;
            const int y = horizontal ? row : out;
            uint8_t* texel = &dst.pixels[(std::size_t(y) * dst.width + x) * 4];
            for (int c = 0; c < 4; ++c) texel[c] = clampByte(sum[c] / weightSum);
        }
    }
}

struct BlockRGBA {
    uint8_t texel[16][4];
};

BlockRGBA gatherBlock(const Image& image, int bx, int by) {
    BlockRGBA block{};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int sx = std::min(bx * 4 + x, image.width - 1);
            const int sy = std::min(by * 4 + y, image.height - 1);
            const uint8_t* from = &image.pixels[(std::size_t(sy) * image.width + sx) * 4];
            std::memcpy(block.texel[y * 4 + x], from, 4);
        }
    }
    return block;
}

uint16_t pack565(int r, int g, int b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void unpack565(uint16_t value, int out[3]) {
    out[0] = ((value >> 11) & 0x1F) * 255 / 31;
    out[1] = ((value >> 5) & 0x3F) * 255 / 63;
    out[2] = (value & 0x1F) * 255 / 31;
}

/// Colour half of a block: the bounding box of the texels, then each texel
/// snapped to whichever of the four palette entries is nearest.
///
/// A range fit rather than a cluster fit. The art being compressed here was
/// already block-compressed once by Blizzard, so the ceiling on what a better
/// fit could recover is low, and a bounding box is exact for the common case of
/// a block that is a gradient between two colours.
void encodeColourBlock(const BlockRGBA& block, uint8_t* out) {
    int lo[3] = {255, 255, 255};
    int hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], int(block.texel[i][c]));
            hi[c] = std::max(hi[c], int(block.texel[i][c]));
        }
    }
    // Pull the box in slightly: the endpoints are quantised to 565 and the
    // extremes are the least populated part of the range.
    for (int c = 0; c < 3; ++c) {
        const int inset = (hi[c] - lo[c]) >> 4;
        lo[c] = std::min(lo[c] + inset, 255);
        hi[c] = std::max(hi[c] - inset, 0);
    }

    uint16_t c0 = pack565(hi[0], hi[1], hi[2]);
    uint16_t c1 = pack565(lo[0], lo[1], lo[2]);
    // c0 > c1 selects the four-colour mode, which is the one with no
    // transparent index - alpha is carried separately in BC3.
    if (c0 < c1) std::swap(c0, c1);
    if (c0 == c1) {
        // A flat block: every index can be 0 and the second endpoint is unused.
        out[0] = uint8_t(c0 & 0xFF); out[1] = uint8_t(c0 >> 8);
        out[2] = uint8_t(c1 & 0xFF); out[3] = uint8_t(c1 >> 8);
        out[4] = out[5] = out[6] = out[7] = 0;
        return;
    }

    int palette[4][3];
    unpack565(c0, palette[0]);
    unpack565(c1, palette[1]);
    for (int c = 0; c < 3; ++c) {
        palette[2][c] = (2 * palette[0][c] + palette[1][c]) / 3;
        palette[3][c] = (palette[0][c] + 2 * palette[1][c]) / 3;
    }

    uint32_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        int bestError = INT32_MAX;
        for (int p = 0; p < 4; ++p) {
            int error = 0;
            for (int c = 0; c < 3; ++c) {
                const int diff = int(block.texel[i][c]) - palette[p][c];
                error += diff * diff;
            }
            if (error < bestError) { bestError = error; best = p; }
        }
        indices |= uint32_t(best) << (i * 2);
    }

    out[0] = uint8_t(c0 & 0xFF); out[1] = uint8_t(c0 >> 8);
    out[2] = uint8_t(c1 & 0xFF); out[3] = uint8_t(c1 >> 8);
    out[4] = uint8_t(indices & 0xFF);
    out[5] = uint8_t((indices >> 8) & 0xFF);
    out[6] = uint8_t((indices >> 16) & 0xFF);
    out[7] = uint8_t((indices >> 24) & 0xFF);
}

/// Alpha half of a block: two endpoints and three bits per texel.
void encodeAlphaBlock(const BlockRGBA& block, uint8_t* out) {
    uint8_t lo = 255;
    uint8_t hi = 0;
    for (int i = 0; i < 16; ++i) {
        lo = std::min(lo, block.texel[i][3]);
        hi = std::max(hi, block.texel[i][3]);
    }
    out[0] = hi;
    out[1] = lo;

    int palette[8];
    if (hi > lo) {
        palette[0] = hi;
        palette[1] = lo;
        for (int i = 0; i < 6; ++i) {
            palette[2 + i] = ((6 - i) * hi + (1 + i) * lo) / 7;
        }
    } else {
        // Flat alpha: every index selects the same value.
        for (int& value : palette) value = hi;
    }

    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        int bestError = INT32_MAX;
        for (int p = 0; p < 8; ++p) {
            const int error = std::abs(int(block.texel[i][3]) - palette[p]);
            if (error < bestError) { bestError = error; best = p; }
        }
        bits |= uint64_t(best) << (i * 3);
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = uint8_t((bits >> (i * 8)) & 0xFF);
}

}  // namespace

Image resampleLanczos(const Image& src, int width, int height) {
    Image out;
    if (!src.valid() || width <= 0 || height <= 0) return out;

    // Horizontally first into a strip, then vertically: two one-dimensional
    // passes rather than one two-dimensional one.
    Image strip;
    strip.width = width;
    strip.height = src.height;
    strip.pixels.assign(std::size_t(width) * src.height * 4, 0);
    resamplePass(src, strip, true);

    out.width = width;
    out.height = height;
    out.pixels.assign(std::size_t(width) * height * 4, 0);
    resamplePass(strip, out, false);
    return out;
}

void sharpenColour(Image& image, float amount, int radius) {
    if (!image.valid() || amount <= 0.0f || radius < 1) return;
    const Image original = image;
    const int w = image.width;
    const int h = image.height;

    // A box blur of the given radius stands in for the gaussian: the difference
    // between the two is well under what the following quantisation to 565
    // will throw away anyway.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sum[3] = {0, 0, 0};
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sx = std::clamp(x + dx, 0, w - 1);
                    const int sy = std::clamp(y + dy, 0, h - 1);
                    const uint8_t* texel = &original.pixels[(std::size_t(sy) * w + sx) * 4];
                    for (int c = 0; c < 3; ++c) sum[c] += texel[c];
                    ++count;
                }
            }
            uint8_t* texel = &image.pixels[(std::size_t(y) * w + x) * 4];
            const uint8_t* from = &original.pixels[(std::size_t(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c) {
                const float blurred = float(sum[c]) / float(count);
                texel[c] = clampByte(float(from[c]) + amount * (float(from[c]) - blurred));
            }
        }
    }
}

void dilateColour(Image& image, int passes, uint8_t threshold) {
    if (!image.valid()) return;
    const int w = image.width;
    const int h = image.height;

    std::vector<uint8_t> known(std::size_t(w) * h, 0);
    for (std::size_t i = 0; i < known.size(); ++i) {
        known[i] = image.pixels[i * 4 + 3] > threshold ? 1 : 0;
    }

    for (int pass = 0; pass < passes; ++pass) {
        std::vector<uint8_t> nextKnown = known;
        std::vector<uint8_t> pixels = image.pixels;
        bool changed = false;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t at = std::size_t(y) * w + x;
                if (known[at]) continue;
                int sum[3] = {0, 0, 0};
                int count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int sx = x + dx;
                        const int sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
                        const std::size_t from = std::size_t(sy) * w + sx;
                        if (!known[from]) continue;
                        for (int c = 0; c < 3; ++c) sum[c] += image.pixels[from * 4 + c];
                        ++count;
                    }
                }
                if (count == 0) continue;
                for (int c = 0; c < 3; ++c) {
                    pixels[at * 4 + c] = static_cast<uint8_t>(sum[c] / count);
                }
                nextKnown[at] = 1;
                changed = true;
            }
        }
        image.pixels.swap(pixels);
        known.swap(nextKnown);
        if (!changed) break;
    }
}

float alphaCoverage(const Image& image, float cutoff) {
    if (!image.valid()) return 0.0f;
    const auto limit = static_cast<uint8_t>(std::clamp(cutoff * 255.0f, 0.0f, 255.0f));
    std::size_t above = 0;
    const std::size_t texels = std::size_t(image.width) * image.height;
    for (std::size_t i = 0; i < texels; ++i) {
        if (image.pixels[i * 4 + 3] >= limit) ++above;
    }
    return texels ? float(above) / float(texels) : 0.0f;
}

void restoreCoverage(Image& image, float target, float cutoff) {
    if (!image.valid() || target <= 0.0f || target >= 1.0f) return;

    // Bisection on the multiplier, which is monotonic in coverage.
    float low = 0.25f;
    float high = 4.0f;
    float best = 1.0f;
    for (int step = 0; step < 16; ++step) {
        const float mid = 0.5f * (low + high);
        Image probe = image;
        const std::size_t texels = std::size_t(probe.width) * probe.height;
        for (std::size_t i = 0; i < texels; ++i) {
            probe.pixels[i * 4 + 3] = clampByte(float(image.pixels[i * 4 + 3]) * mid);
        }
        if (alphaCoverage(probe, cutoff) < target) low = mid; else high = mid;
        best = mid;
    }
    const std::size_t texels = std::size_t(image.width) * image.height;
    for (std::size_t i = 0; i < texels; ++i) {
        image.pixels[i * 4 + 3] = clampByte(float(image.pixels[i * 4 + 3]) * best);
    }
}

Image halveStraight(const Image& image) {
    const int w = std::max(1, image.width / 2);
    const int h = std::max(1, image.height / 2);
    return resampleLanczos(image, w, h);
}

std::vector<Image> buildMipChain(const Image& base, float cutoff) {
    std::vector<Image> levels;
    if (!base.valid()) return levels;
    levels.push_back(base);

    const float coverage = alphaCoverage(base, cutoff);
    Image current = base;
    while (current.width > 1 || current.height > 1) {
        current = halveStraight(current);
        if (!current.valid()) break;
        if (coverage > 0.0f && coverage < 1.0f) restoreCoverage(current, coverage, cutoff);
        levels.push_back(current);
    }
    return levels;
}

std::vector<uint8_t> encodeBC3(const Image& image) {
    std::vector<uint8_t> out;
    if (!image.valid()) return out;
    const int bw = (image.width + 3) / 4;
    const int bh = (image.height + 3) / 4;
    out.resize(std::size_t(bw) * bh * 16);

    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const BlockRGBA block = gatherBlock(image, bx, by);
            uint8_t* at = &out[(std::size_t(by) * bw + bx) * 16];
            encodeAlphaBlock(block, at);
            encodeColourBlock(block, at + 8);
        }
    }
    return out;
}

std::vector<uint8_t> writeDDS(const std::vector<Image>& levels) {
    std::vector<uint8_t> out;
    if (levels.empty() || !levels.front().valid()) return out;

    std::vector<std::vector<uint8_t>> payload;
    std::size_t total = 0;
    for (const Image& level : levels) {
        payload.push_back(encodeBC3(level));
        total += payload.back().size();
    }

    out.resize(128, 0);
    const auto put32 = [&out](std::size_t at, uint32_t value) {
        out[at + 0] = uint8_t(value & 0xFF);
        out[at + 1] = uint8_t((value >> 8) & 0xFF);
        out[at + 2] = uint8_t((value >> 16) & 0xFF);
        out[at + 3] = uint8_t((value >> 24) & 0xFF);
    };
    std::memcpy(out.data(), "DDS ", 4);
    put32(4, 124);                                  // header size
    put32(8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
    put32(12, uint32_t(levels.front().height));
    put32(16, uint32_t(levels.front().width));
    put32(20, uint32_t(payload.front().size()));    // linear size
    put32(28, uint32_t(levels.size()));             // mip count
    put32(76, 32);                                  // pixel format size
    put32(80, 0x4);                                 // FOURCC
    std::memcpy(out.data() + 84, "DXT5", 4);
    put32(108, 0x1000 | 0x400000 | 0x8);            // texture | mipmap | complex

    out.reserve(out.size() + total);
    for (const std::vector<uint8_t>& level : payload) {
        out.insert(out.end(), level.begin(), level.end());
    }
    return out;
}

}  // namespace wowee::assets
