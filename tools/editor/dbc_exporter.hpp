#pragma once

#include "pipeline/dbc_loader.hpp"
#include <string>
#include <vector>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace editor {

class DBCExporter {
public:
    // Export a DBC file as JSON with field names from layout
    static bool exportAsJson(pipeline::AssetManager* am,
                              const std::string& dbcName,
                              const std::string& outputPath);

    // Export all zone-relevant DBCs to JSON
    static int exportZoneDBCs(pipeline::AssetManager* am,
                               const std::string& outputDir);
};

} // namespace editor
} // namespace wowee
