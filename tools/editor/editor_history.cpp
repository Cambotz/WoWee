#include "editor_history.hpp"

namespace wowee {
namespace editor {

ChunkSnapshot EditorHistory::captureChunk(const pipeline::ADTTerrain& terrain, int idx) {
    ChunkSnapshot snap;
    // ADTTerrain.chunks is std::array<MapChunk, 256>; out-of-range
    // would be undefined behaviour. Return an empty snapshot instead.
    if (idx < 0 || idx >= 256) return snap;
    snap.chunkIndex = idx;
    snap.heights = terrain.chunks[idx].heightMap.heights;
    snap.alphaMap = terrain.chunks[idx].alphaMap;
    snap.layers = terrain.chunks[idx].layers;
    return snap;
}

void EditorHistory::restoreChunk(pipeline::ADTTerrain& terrain, const ChunkSnapshot& snap) {
    if (snap.chunkIndex < 0 || snap.chunkIndex >= 256) return;
    terrain.chunks[snap.chunkIndex].heightMap.heights = snap.heights;
    terrain.chunks[snap.chunkIndex].alphaMap = snap.alphaMap;
    terrain.chunks[snap.chunkIndex].layers = snap.layers;
}

bool EditorHistory::snapshotChanged(const ChunkSnapshot& a, const ChunkSnapshot& b) {
    if (a.heights != b.heights) return true;
    if (a.alphaMap != b.alphaMap) return true;
    if (a.layers.size() != b.layers.size()) return true;
    for (size_t i = 0; i < a.layers.size(); i++) {
        if (a.layers[i].textureId != b.layers[i].textureId) return true;
    }
    return false;
}

void EditorHistory::beginEdit(const pipeline::ADTTerrain& terrain,
                               const std::vector<int>& affectedChunks) {
    pending_ = {};
    pending_.before.reserve(affectedChunks.size());
    for (int idx : affectedChunks) {
        pending_.before.push_back(captureChunk(terrain, idx));
    }
}

void EditorHistory::endEdit(const pipeline::ADTTerrain& terrain) {
    pending_.after.reserve(pending_.before.size());
    lastAffected_.clear();
    for (const auto& snap : pending_.before) {
        pending_.after.push_back(captureChunk(terrain, snap.chunkIndex));
        lastAffected_.push_back(snap.chunkIndex);
    }

    bool changed = false;
    for (size_t i = 0; i < pending_.before.size(); i++) {
        if (snapshotChanged(pending_.before[i], pending_.after[i])) {
            changed = true;
            break;
        }
    }
    if (!changed) return;

    undoStack_.push_back(std::move(pending_));
    redoStack_.clear();

    if (undoStack_.size() > MAX_UNDO)
        undoStack_.erase(undoStack_.begin());
}

void EditorHistory::undo(pipeline::ADTTerrain& terrain) {
    if (undoStack_.empty()) return;

    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();

    lastAffected_.clear();
    for (const auto& snap : cmd.before) {
        restoreChunk(terrain, snap);
        lastAffected_.push_back(snap.chunkIndex);
    }

    redoStack_.push_back(std::move(cmd));
}

void EditorHistory::redo(pipeline::ADTTerrain& terrain) {
    if (redoStack_.empty()) return;

    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();

    lastAffected_.clear();
    for (const auto& snap : cmd.after) {
        restoreChunk(terrain, snap);
        lastAffected_.push_back(snap.chunkIndex);
    }

    undoStack_.push_back(std::move(cmd));
}

void EditorHistory::clear() {
    undoStack_.clear();
    redoStack_.clear();
    lastAffected_.clear();
}

} // namespace editor
} // namespace wowee
