#pragma once

#include "pipeline/adt_loader.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

class ADTWriter {
public:
    static bool write(const pipeline::ADTTerrain& terrain, const std::string& path);
    static std::vector<uint8_t> serialize(const pipeline::ADTTerrain& terrain);

    // Write a minimal WDT file for a single-tile map
    static bool writeWDT(const std::string& mapName, int tileX, int tileY, const std::string& path);

private:
    static void writeChunkHeader(std::vector<uint8_t>& buf, uint32_t magic, uint32_t size);
    static void writeU32(std::vector<uint8_t>& buf, uint32_t val);
    static void writeU16(std::vector<uint8_t>& buf, uint16_t val);
    static void writeFloat(std::vector<uint8_t>& buf, float val);
    static void writeBytes(std::vector<uint8_t>& buf, const void* data, size_t size);
    static void patchSize(std::vector<uint8_t>& buf, size_t headerOffset);

    static void writeMVER(std::vector<uint8_t>& buf);
    static void writeMHDR(std::vector<uint8_t>& buf, size_t& mhdrOffset);
    static void writeMTEX(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain);
    static void writeMMDX(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain);
    static void writeMWMO(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain);
    static void writeMDDF(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain);
    static void writeMODF(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain);
    static void writeMCNK(std::vector<uint8_t>& buf, const pipeline::MapChunk& chunk, int chunkX, int chunkY);
};

} // namespace editor
} // namespace wowee
