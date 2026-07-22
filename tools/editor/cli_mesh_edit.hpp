#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the WOM mesh editing/transform commands:
//   --add-texture-to-mesh     --scale-mesh
//   --translate-mesh          --strip-mesh
//   --rotate-mesh             --center-mesh
//   --flip-mesh-normals       --mirror-mesh
//   --smooth-mesh-normals     --merge-meshes
//
// Returns true if matched; outRc holds the exit code.
bool handleMeshEdit(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
