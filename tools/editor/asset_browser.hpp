#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace editor {

struct AssetEntry {
    std::string wowPath;
    std::string displayName;
    std::string directory;
};

class AssetBrowser {
public:
    void initialize(pipeline::AssetManager* am);

    const std::vector<AssetEntry>& getTextures() const { return textures_; }
    const std::vector<AssetEntry>& getM2Models() const { return m2Models_; }
    const std::vector<AssetEntry>& getWMOs() const { return wmos_; }

    const std::vector<std::string>& getTextureDirectories() const { return textureDirs_; }
    const std::vector<std::string>& getM2Directories() const { return m2Dirs_; }
    const std::vector<std::string>& getWMODirectories() const { return wmoDirs_; }

    const std::vector<std::string>& getMapNames() const { return mapNames_; }
    std::vector<std::pair<int,int>> getMapTiles(const std::string& mapName) const;

    bool isInitialized() const { return initialized_; }

private:
    static std::string extractFilename(const std::string& path);
    static std::string extractDirectory(const std::string& path);

    std::vector<AssetEntry> textures_;
    std::vector<AssetEntry> m2Models_;
    std::vector<AssetEntry> wmos_;
    std::vector<std::string> textureDirs_;
    std::vector<std::string> m2Dirs_;
    std::vector<std::string> wmoDirs_;
    std::vector<std::string> mapNames_;
    bool initialized_ = false;
};

} // namespace editor
} // namespace wowee
