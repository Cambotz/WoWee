#pragma once

#include "pipeline/adt_loader.hpp"
#include <vector>
#include <array>
#include <memory>

namespace wowee {
namespace editor {

struct ChunkSnapshot {
    int chunkIndex;
    std::array<float, 145> heights;
    std::vector<uint8_t> alphaMap;
    std::vector<pipeline::TextureLayer> layers;
};

struct EditCommand {
    std::vector<ChunkSnapshot> before;
    std::vector<ChunkSnapshot> after;
};

class EditorHistory {
public:
    void beginEdit(const pipeline::ADTTerrain& terrain, const std::vector<int>& affectedChunks);
    void endEdit(const pipeline::ADTTerrain& terrain);

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    void undo(pipeline::ADTTerrain& terrain);
    void redo(pipeline::ADTTerrain& terrain);

    void clear();

    size_t undoCount() const { return undoStack_.size(); }
    size_t redoCount() const { return redoStack_.size(); }

    const std::vector<int>& lastAffectedChunks() const { return lastAffected_; }

private:
    static ChunkSnapshot captureChunk(const pipeline::ADTTerrain& terrain, int idx);
    static void restoreChunk(pipeline::ADTTerrain& terrain, const ChunkSnapshot& snap);
    static bool snapshotChanged(const ChunkSnapshot& a, const ChunkSnapshot& b);

    std::vector<EditCommand> undoStack_;
    std::vector<EditCommand> redoStack_;
    EditCommand pending_;
    std::vector<int> lastAffected_;
    static constexpr size_t MAX_UNDO = 100;
};

} // namespace editor
} // namespace wowee
