#pragma once

#include "editor_camera.hpp"
#include "editor_viewport.hpp"
#include "editor_ui.hpp"
#include "terrain_biomes.hpp"
#include "terrain_editor.hpp"
#include "texture_painter.hpp"
#include "object_placer.hpp"
#include "npc_spawner.hpp"
#include "npc_presets.hpp"
#include "quest_editor.hpp"
#include "editor_project.hpp"
#include "zone_manifest.hpp"
#include "asset_browser.hpp"
#include "core/window.hpp"
#include "pipeline/asset_manager.hpp"
#include <string>
#include <memory>

namespace wowee {
namespace editor {

enum class EditorMode { Sculpt, Paint, PlaceObject, Water, NPC, Quest };

class EditorApp {
public:
    EditorApp();
    ~EditorApp();

    bool initialize(const std::string& dataPath);
    void run();
    void shutdown();

    void loadADT(const std::string& mapName, int tileX, int tileY);
    bool loadWMOInstance(const std::string& mapName);
    void createNewTerrain(const std::string& mapName, int tileX, int tileY, float baseHeight, Biome biome);
    void saveADT(const std::string& path);
    void saveWDT(const std::string& path);
    void exportZone(const std::string& outputDir);
    void quickSave();
    void exportContentPack(const std::string& destPath);
    void exportOpenFormat(const std::string& basePath);

    void requestQuit();
    void resetCamera();
    void setWireframe(bool enabled);
    bool isWireframe() const;

    EditorCamera& getEditorCamera() { return camera_; }
    TerrainEditor& getTerrainEditor() { return terrainEditor_; }
    TexturePainter& getTexturePainter() { return texturePainter_; }
    ObjectPlacer& getObjectPlacer() { return objectPlacer_; }
    NpcSpawner& getNpcSpawner() { return npcSpawner_; }
    NpcPresets& getNpcPresets() { return npcPresets_; }
    QuestEditor& getQuestEditor() { return questEditor_; }
    AssetBrowser& getAssetBrowser() { return assetBrowser_; }
    EditorViewport& getViewport() { return viewport_; }
    bool setMapName(const std::string& name) {
        if (name.empty()) return false;
        for (char c : name) {
            if (c == '/' || c == '\\' || c == '.' || c == ':' ||
                c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|' || c < 32) return false;
        }
        loadedMap_ = name;
        return true;
    }
    rendering::TerrainRenderer* getTerrainRenderer();
    rendering::M2Renderer* getM2Renderer() { return viewport_.getM2Renderer(); }
    pipeline::AssetManager* getAssetManager() { return assetManager_.get(); }

    const std::string& getLoadedMap() const { return loadedMap_; }
    int getLoadedTileX() const { return loadedTileX_; }
    int getLoadedTileY() const { return loadedTileY_; }
    bool hasTerrainLoaded() const { return terrain_.isLoaded(); }

    core::Window* getWindow() { return window_.get(); }

    EditorMode getMode() const { return mode_; }
    void setMode(EditorMode m) {
        if (m != mode_) {
            viewport_.clearGhostPreview();
            viewport_.setBrushIndicator({}, 0, false);
        }
        mode_ = m;
    }
    void markObjectsDirty() { objectsDirty_ = true; autoSavePendingChanges_ = true; }
    bool hasUnsavedNonTerrainChanges() const { return autoSavePendingChanges_; }

    void startGizmoMode(TransformMode mode);
    void setGizmoAxis(TransformAxis axis);
    void setSkyPreset(int preset); // 0=day, 1=dusk, 2=night
    void snapSelectedToGround();
    void alignSelectedToTerrain();
    void flattenAroundSelected(float radius = 30.0f);
    void flyToSelected();
    int batchConvertAssets(const std::string& dataDir);
    void clearAllObjects();
    void generateCompleteZone();
    // Drop N random creatures + M random objects within the loaded
    // tile's world bbox. Mirrors the --random-populate-zone CLI
    // command so users can do bulk population from inside the editor.
    void randomPopulateZone(int creatureCount, int objectCount, uint32_t seed);
    // Re-snap every creature + object Z to actual terrain. In-editor
    // mirror of the --snap-zone-to-ground CLI; useful after terrain
    // edits or random population to fix floating/buried spawns.
    void snapAllSpawnsToGround();
    // Count spawns whose Z is more than `threshold` yards off from
    // the terrain. Returns the issue count; 0 means clean. Used by
    // the in-editor "Audit Spawns" menu to surface placement bugs
    // without dropping to CLI.
    int auditSpawnsAgainstTerrain(float threshold = 5.0f) const;
    void centerOnTerrain();

    // Multi-tile support
    void addAdjacentTile(int offsetX, int offsetY);

    // Camera bookmarks
    struct CameraBookmark { glm::vec3 pos; float yaw; float pitch; std::string name; };
    void saveBookmark(const std::string& name);
    void loadBookmark(int index);
    const std::vector<CameraBookmark>& getBookmarks() const { return bookmarks_; }
    TransformGizmo& getGizmo() { return viewport_.getGizmo(); }
    bool shouldOpenContextMenu() const { return openContextMenu_; }
    void clearContextMenuFlag() { openContextMenu_ = false; }

    float getWaterHeight() const { return waterHeight_; }
    void setWaterHeight(float h) { waterHeight_ = h; }
    uint16_t getWaterType() const { return waterType_; }
    void setWaterType(uint16_t t) { waterType_ = t; }

private:
    void processEvents();
    void updateTerrainEditing(float dt);
    void refreshDirtyChunks();
    void initImGui();
    void shutdownImGui();

    std::unique_ptr<core::Window> window_;
    std::unique_ptr<pipeline::AssetManager> assetManager_;
    EditorCamera camera_;
    EditorViewport viewport_;
    EditorUI ui_;
    TerrainEditor terrainEditor_;
    TexturePainter texturePainter_;
    ObjectPlacer objectPlacer_;
    NpcSpawner npcSpawner_;
    NpcPresets npcPresets_;
    QuestEditor questEditor_;
    EditorProject project_;
public:
    EditorProject& getProject() { return project_; }
private:
    AssetBrowser assetBrowser_;

    pipeline::ADTTerrain terrain_;

    bool imguiInitialized_ = false;
    bool painting_ = false;
    bool objectsDirty_ = false;
    bool openContextMenu_ = false;
    std::string lastSavePath_;
    std::vector<CameraBookmark> bookmarks_;
    float autoSaveTimer_ = 0.0f;
    float autoSaveInterval_ = 300.0f;
    bool autoSaveEnabled_ = true;
    bool autoSavePendingChanges_ = false;
    bool showQuitConfirm_ = false;
    ZoneManifest zoneManifest_;

    // Recent zones
    struct RecentZone { std::string mapName; int tileX; int tileY; };
    std::vector<RecentZone> recentZones_;

    // Toast notifications
    struct Toast { std::string msg; float timer; };
    std::vector<Toast> toasts_;
public:
    void showToast(const std::string& msg, float duration = 3.0f);
    const std::vector<Toast>& getToasts() const { return toasts_; }
    const std::vector<RecentZone>& getRecentZones() const { return recentZones_; }
    ZoneManifest& getZoneManifest() { return zoneManifest_; }
    bool isAutoSaveEnabled() const { return autoSaveEnabled_; }
    void setAutoSaveEnabled(bool v) { autoSaveEnabled_ = v; }
    float getAutoSaveInterval() const { return autoSaveInterval_; }
    void setAutoSaveInterval(float s) { autoSaveInterval_ = std::clamp(s, 60.0f, 900.0f); }
    float getAutoSaveTimeRemaining() const { return autoSaveInterval_ - autoSaveTimer_; }
    void updateToasts(float dt);
private:
    size_t lastObjCount_ = 0;
    size_t lastNpcCount_ = 0;
    int lastNpcSelIdx_ = -1;
    EditorMode mode_ = EditorMode::Sculpt;
    float waterHeight_ = 100.0f;
    uint16_t waterType_ = 0;
    std::string dataPath_;
public:
    const std::string& getDataPath() const { return dataPath_; }
private:

    std::string loadedMap_;
    int loadedTileX_ = -1;
    int loadedTileY_ = -1;

    // Last biome the user selected via createNewTerrain. generateCompleteZone()
    // reads this so subsequent generations honor the active biome instead of
    // reapplying the same hardcoded heightband textures every time.
    Biome activeBiome_ = Biome::Grassland;
public:
    Biome getActiveBiome() const { return activeBiome_; }
private:

    // "Click on terrain to place crater" mode. The Crater button arms this
    // with the user's chosen radius/depth/rim; the next left-click on
    // terrain consumes it and creates the crater at the actual click
    // position. Solves the UX problem where pressing the button used a
    // stale brush position because the cursor was on the button itself.
public:
    struct PendingCrater {
        bool active = false;
        float radius = 30.0f;
        float depth = 10.0f;
        float rim = 3.0f;
    };
    PendingCrater& pendingCrater() { return pendingCrater_; }
private:
    PendingCrater pendingCrater_;
};

} // namespace editor
} // namespace wowee
