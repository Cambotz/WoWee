#pragma once

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "quest_editor.hpp"
#include <string>
#include <vector>

namespace wowee {
namespace editor {

// Generates a complete AzerothCore server module from editor zone data
// Output: SQL + worldserver.conf snippet + README for server admins
class ServerModuleGenerator {
public:
    struct Config {
        uint32_t mapId = 9000;
        uint32_t startCreatureEntry = 100000;
        uint32_t startQuestEntry = 100000;
        uint32_t zoneId = 10000;
        uint32_t areaId = 10001;
        std::string mapName;
        std::string displayName;
    };

    // Generate complete server module directory
    static bool generate(const ZoneManifest& manifest,
                          const std::vector<CreatureSpawn>& creatures,
                          const std::vector<Quest>& quests,
                          const std::string& outputDir);
};

} // namespace editor
} // namespace wowee
