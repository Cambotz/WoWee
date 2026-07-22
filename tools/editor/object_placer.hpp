#pragma once

#include "pipeline/adt_loader.hpp"
#include "rendering/camera.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

enum class PlaceableType { M2, WMO };

struct PlacedObject {
    PlaceableType type;
    std::string path;
    uint32_t nameId;
    uint32_t uniqueId;
    glm::vec3 position;
    glm::vec3 rotation; // degrees
    float scale;        // 1.0 = normal
    bool selected = false;
};

class ObjectPlacer {
public:
    void setTerrain(pipeline::ADTTerrain* terrain) { terrain_ = terrain; }

    void setActivePath(const std::string& path, PlaceableType type);
    const std::string& getActivePath() const { return activePath_; }
    PlaceableType getActiveType() const { return activeType_; }

    // Place object at world position
    void placeObject(const glm::vec3& position);

    // Select object nearest to ray (Shift adds to selection)
    int selectAt(const rendering::Ray& ray, float maxDist = 50.0f);
    void addToSelection(int idx);
    void toggleSelection(int idx);
    void clearSelection();
    int getSelectedIndex() const { return selectedIdx_; }
    PlacedObject* getSelected();
    const std::vector<int>& getSelectedIndices() const { return selectedIndices_; }
    size_t selectionCount() const { return selectedIndices_.size(); }
    bool isMultiSelected() const { return selectedIndices_.size() > 1; }

    // Transform selected (operates on all selected objects)
    void moveSelected(const glm::vec3& delta);
    void rotateSelected(const glm::vec3& deltaDeg);
    void scaleSelected(float delta);
    void deleteSelected();

    // Sync placed objects back to ADTTerrain structs
    void syncToTerrain();

    // Save/load placed objects to JSON
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    const std::vector<PlacedObject>& getObjects() const { return objects_; }
    std::vector<PlacedObject>& getObjects() { return objects_; }
    void selectAll();
    void selectByType(PlaceableType type);
    void clearAll() { objects_.clear(); undoStack_.clear(); selectedIdx_ = -1; selectedIndices_.clear(); uniqueIdCounter_ = 1; }
    size_t objectCount() const { return objects_.size(); }

    float getPlacementRotationY() const { return placementRotY_; }
    void setPlacementRotationY(float deg) { placementRotY_ = deg; }
    float getPlacementScale() const { return placementScale_; }
    void setPlacementScale(float s) { placementScale_ = s; }
    bool getRandomRotation() const { return randomRotation_; }
    void setRandomRotation(bool v) { randomRotation_ = v; }
    bool getSnapToGround() const { return snapToGround_; }
    void setSnapToGround(bool v) { snapToGround_ = v; }

    // Undo last placement
    bool canUndoPlace() const { return !undoStack_.empty(); }
    void undoLastPlace();

    // Scatter: place multiple copies with random offset/rotation
    void scatter(const glm::vec3& center, float radius, int count,
                 float minScale, float maxScale);

    // Procedural biome population: auto-place vegetation based on rules
    int populateBiome(const struct BiomeVegetation& vegetation,
                       float tileSize, const glm::vec3& tileOrigin,
                       uint32_t seed = 42);

private:
    uint32_t nextUniqueId();

    pipeline::ADTTerrain* terrain_ = nullptr;
    std::string activePath_;
    PlaceableType activeType_ = PlaceableType::M2;

    std::vector<PlacedObject> objects_;
    std::vector<int> undoStack_; // indices of recently placed objects
    int selectedIdx_ = -1;
    std::vector<int> selectedIndices_;
    uint32_t uniqueIdCounter_ = 1;
    float placementRotY_ = 0.0f;
    float placementScale_ = 1.0f;
    bool randomRotation_ = false;
    bool snapToGround_ = true;
};

} // namespace editor
} // namespace wowee
