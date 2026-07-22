#pragma once

#include "editor_brush.hpp"
#include "pipeline/adt_loader.hpp"
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace wowee {
namespace editor {

class TexturePainter {
public:
    void setTerrain(pipeline::ADTTerrain* terrain) { terrain_ = terrain; }

    void setActiveTexture(const std::string& texturePath);
    const std::string& getActiveTexture() const { return activeTexture_; }
    const std::vector<std::string>& getRecentTextures() const { return recentTextures_; }

    // Auto-paint textures based on terrain height bands
    struct HeightBand { float maxHeight; std::string texturePath; };
    void autoPaintByHeight(const std::vector<HeightBand>& bands);

    // Auto-paint steep slopes with rock texture
    void autoPaintBySlope(float slopeThreshold, const std::string& steepTexture);

    // Paint a texture along a line (for roads/paths after flattening)
    void paintAlongPath(const glm::vec3& start, const glm::vec3& end,
                        float width, const std::string& texturePath);

    // Gradient blend: transitions base texture from one to another across tile
    void gradientBlend(const std::string& tex1, const std::string& tex2, bool horizontal);

    // Scatter random texture patches across the terrain
    void scatterPatches(const std::string& texturePath, int count, float minRadius,
                        float maxRadius, uint32_t seed);

    // Paint the active texture at the given world position
    // Returns list of modified chunk indices
    std::vector<int> paint(const glm::vec3& center, float radius, float strength, float falloff);

    // Erase a texture layer at the given position
    std::vector<int> erase(const glm::vec3& center, float radius, float strength, float falloff);

    // Pick the dominant texture at a world position (eyedropper)
    std::string pickTextureAt(const glm::vec3& worldPos) const;

private:
    uint32_t ensureTextureInList(const std::string& path);
    int ensureLayerOnChunk(int chunkIdx, uint32_t textureId);
    void modifyAlpha(int chunkIdx, int layerIdx, const glm::vec3& center,
                     float radius, float strength, float falloff, bool erasing);

    glm::vec2 worldToChunkUV(int chunkIdx, const glm::vec3& worldPos) const;

    pipeline::ADTTerrain* terrain_ = nullptr;
    std::string activeTexture_;
    std::vector<std::string> recentTextures_;

    static constexpr float TILE_SIZE = 533.33333f;
    static constexpr float CHUNK_SIZE = TILE_SIZE / 16.0f;
};

} // namespace editor
} // namespace wowee
