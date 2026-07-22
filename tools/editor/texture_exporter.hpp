#pragma once

#include "pipeline/adt_loader.hpp"
#include <string>
#include <vector>
#include <unordered_set>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace editor {

class TextureExporter {
public:
    // Collect all texture paths referenced by the terrain
    static std::vector<std::string> collectUsedTextures(const pipeline::ADTTerrain& terrain);

    // Collect all texture paths referenced by an M2 model (loads the M2 from `am`).
    // Returns lowercased game paths (e.g. "creature\\foo\\foo.blp"). Empty if M2 not found.
    static std::vector<std::string> collectM2Textures(pipeline::AssetManager* am,
                                                       const std::string& m2Path);

    // Collect all texture paths referenced by a WMO root file.
    // Includes textures used by every group (loads root + group files via `am`).
    static std::vector<std::string> collectWMOTextures(pipeline::AssetManager* am,
                                                        const std::string& wmoPath);

    // Export all used textures as PNG to an output directory
    // Returns count of successfully exported textures
    static int exportTexturesAsPng(pipeline::AssetManager* am,
                                    const std::vector<std::string>& texturePaths,
                                    const std::string& outputDir);
};

} // namespace editor
} // namespace wowee
