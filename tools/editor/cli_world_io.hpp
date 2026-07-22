#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the world-asset interchange handlers. WOB / WHM / WOC
// are our open replacements for proprietary WMO / ADT-heightmap /
// ADT-collision data; these handlers bridge them to the formats
// every other 3D tool understands so the open-format ecosystem
// is actually usable.
//   --export-wob-glb    WOB -> glTF 2.0 binary
//   --export-wob-obj    WOB -> Wavefront OBJ (per-group)
//   --import-wob-obj    Wavefront OBJ -> WOB (preserves doodads)
//   --export-whm-glb    WHM/WOT terrain -> glTF 2.0 (per-chunk)
//   --export-whm-obj    WHM/WOT terrain -> Wavefront OBJ
//   --export-woc-obj    WOC collision -> OBJ (grouped by walkability)
//
// Returns true if matched; outRc holds the exit code.
bool handleWorldIo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
