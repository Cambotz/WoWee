#include "upscale.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "pipeline/blp_loader.hpp"
#include "rendering/m2_model_classifier.hpp"
#include "texture_ops.hpp"

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;

std::vector<uint8_t> slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint32_t readLE32(const uint8_t* at) {
    return uint32_t(at[0]) | (uint32_t(at[1]) << 8) |
           (uint32_t(at[2]) << 16) | (uint32_t(at[3]) << 24);
}

float readFloat(const uint8_t* at) {
    float out = 0.0f;
    std::memcpy(&out, at, 4);
    return out;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// The texture paths an M2 names, read straight out of its header.
///
/// The full loader would do this too and a great deal else; a survey of every
/// model in an installation only needs the names.
std::vector<std::string> modelTextures(const std::vector<uint8_t>& blob) {
    std::vector<std::string> out;
    if (blob.size() < 88) return out;
    const uint32_t count = readLE32(blob.data() + 80);
    const uint32_t offset = readLE32(blob.data() + 84);
    if (count > 256) return out;                 // not a header this understands
    for (uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = offset + std::size_t(i) * 16;
        if (entry + 16 > blob.size()) break;
        const uint32_t length = readLE32(blob.data() + entry + 8);
        const uint32_t at = readLE32(blob.data() + entry + 12);
        if (length == 0 || at + length > blob.size()) continue;
        std::string name(reinterpret_cast<const char*>(blob.data() + at), length);
        const std::size_t nul = name.find('\0');
        if (nul != std::string::npos) name.resize(nul);
        if (!name.empty()) out.push_back(std::move(name));
    }
    return out;
}

/// The model's own bounds, for the classifier.
bool modelBounds(const std::vector<uint8_t>& blob, float lo[3], float hi[3],
                 uint32_t& vertices) {
    if (blob.size() < 0xD0) return false;
    vertices = readLE32(blob.data() + 60);
    // The bounding box sits after the sequence/bone/vertex arrays; 0xB8 is
    // where it lands in the 3.3.5 header this reads.
    for (int i = 0; i < 3; ++i) {
        lo[i] = readFloat(blob.data() + 0xB8 + i * 4);
        hi[i] = readFloat(blob.data() + 0xC4 + i * 4);
    }
    return true;
}

fs::path sidecarFor(const fs::path& blp) {
    fs::path out = blp;
    out.replace_extension(".dds");
    return out;
}

}  // namespace

UpscalePlan planUpscale(const std::string& expansionDir, int sharedLimit) {
    UpscalePlan plan;
    std::error_code ec;
    if (!fs::is_directory(expansionDir, ec)) return plan;

    // texture path (lowered, game-relative) -> how many foliage models draw it
    std::map<std::string, int> users;
    const fs::path root(expansionDir);

    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (lower(it->path().extension().string()) != ".m2") continue;

        const std::vector<uint8_t> blob = slurp(it->path());
        if (blob.size() < 0xD0) continue;

        float lo[3] = {0, 0, 0};
        float hi[3] = {0, 0, 0};
        uint32_t vertices = 0;
        if (!modelBounds(blob, lo, hi, vertices)) continue;

        const auto classification = wowee::rendering::classifyM2Model(
            it->path().filename().string(),
            glm::vec3(lo[0], lo[1], lo[2]), glm::vec3(hi[0], hi[1], hi[2]),
            vertices, 0);
        if (!classification.isFoliageLike) continue;
        ++plan.foliageModels;

        for (const std::string& texture : modelTextures(blob)) {
            std::string key = lower(texture);
            std::replace(key.begin(), key.end(), '\\', '/');
            ++users[key];
        }
    }

    for (const auto& [relative, count] : users) {
        if (count > sharedLimit) {
            ++plan.sharedSkipped;
            continue;
        }
        const fs::path full = root / relative;
        if (fs::is_regular_file(full, ec)) plan.textures.push_back(full.string());
    }
    return plan;
}

UpscaleResult runUpscale(const UpscalePlan& plan, int scale,
                         const std::function<void(const std::string&)>& say,
                         const std::atomic<bool>& cancel) {
    UpscaleResult result;
    for (const std::string& path : plan.textures) {
        if (cancel.load()) break;

        const fs::path blpPath(path);
        const fs::path ddsPath = sidecarFor(blpPath);
        std::error_code ec;
        if (fs::exists(ddsPath, ec)) {
            ++result.skipped;
            continue;
        }

        const std::vector<uint8_t> bytes = slurp(blpPath);
        if (bytes.empty()) { ++result.failed; continue; }

        const auto decoded = pipeline::BLPLoader::load(bytes, false);
        Image source;
        source.width = decoded.width;
        source.height = decoded.height;
        source.pixels = decoded.data;
        if (!source.valid()) { ++result.failed; continue; }

        // Colour first into the transparent texels, because everything after
        // this filters across them.
        dilateColour(source);
        const float coverage = alphaCoverage(source, 0.5f);

        Image big = resampleLanczos(source, source.width * scale, source.height * scale);
        if (!big.valid()) { ++result.failed; continue; }
        sharpenColour(big, 0.45f, std::max(1, scale));
        if (coverage > 0.0f && coverage < 1.0f) restoreCoverage(big, coverage, 0.5f);

        const std::vector<Image> levels = buildMipChain(big, 0.5f);
        const std::vector<uint8_t> dds = writeDDS(levels);
        if (dds.empty()) { ++result.failed; continue; }

        std::ofstream out(ddsPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(dds.data()),
                  static_cast<std::streamsize>(dds.size()));
        if (!out) { ++result.failed; continue; }

        ++result.written;
        if (say && result.written % 25 == 0) {
            say("    " + std::to_string(result.written) + " of " +
                std::to_string(plan.textures.size()) + " textures");
        }
    }
    return result;
}

}  // namespace wowee::assets
