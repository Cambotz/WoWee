#pragma once

#include "npc_spawner.hpp"
#include <string>
#include <vector>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace editor {

enum class CreatureCategory {
    Critter,
    Beast,
    Humanoid,
    Undead,
    Demon,
    Elemental,
    Dragonkin,
    Giant,
    Mechanical,
    Mount,
    Boss,
    Other,
    COUNT
};

struct NpcPreset {
    std::string name;
    std::string modelPath;
    CreatureCategory category;
    uint32_t defaultLevel;
    uint32_t defaultHealth;
    bool defaultHostile;
};

class NpcPresets {
public:
    void initialize(pipeline::AssetManager* am);

    const std::vector<NpcPreset>& getPresets() const { return presets_; }
    const std::vector<NpcPreset>& getByCategory(CreatureCategory cat) const;

    static const char* getCategoryName(CreatureCategory cat);
    bool isInitialized() const { return initialized_; }

private:
    CreatureCategory classifyCreature(const std::string& dirName) const;
    std::string prettifyName(const std::string& dirName) const;
    uint32_t estimateLevel(const std::string& dirName) const;
    uint32_t estimateHealth(uint32_t level) const;

    std::vector<NpcPreset> presets_;
    std::vector<std::vector<NpcPreset>> byCategory_;
    bool initialized_ = false;
};

} // namespace editor
} // namespace wowee
