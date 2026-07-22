#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the mesh ⇄ heightmap I/O ops:
//   --displace-mesh             (perturb verts along normal by PNG)
//   --gen-mesh-from-heightmap   (generate WOM grid from PNG heightmap)
//   --export-mesh-heightmap     (sample WOM Y to PNG heightmap)
//
// Returns true if matched; outRc holds the exit code.
bool handleMeshIO(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
