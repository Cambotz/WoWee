#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

struct ZoneManifest {
    std::string mapName;
    std::string displayName;
    uint32_t mapId = 9000; // Custom map IDs start high to avoid conflicts
    std::vector<std::pair<int, int>> tiles; // (tileX, tileY) pairs
    std::string biome;
    float baseHeight = 100.0f;
    bool hasCreatures = false;
    std::string description;

    // Zone gameplay flags
    bool allowFlying = false;
    bool pvpEnabled = false;
    bool isIndoor = false;
    bool isSanctuary = false;

    // Audio configuration
    std::string musicTrack;         // Background music file path
    std::string ambienceDay;        // Daytime ambient sound
    std::string ambienceNight;      // Nighttime ambient sound
    float musicVolume = 0.7f;
    float ambienceVolume = 0.5f;

    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

} // namespace editor
} // namespace wowee
