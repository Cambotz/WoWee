#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the five project-wide inventory handlers:
//   --list-project-meshes        (per-zone WOM aggregate)
//   --list-project-meshes-detail (per-mesh sorted-by-tris listing)
//   --list-project-audio         (per-zone WAV aggregate)
//   --list-project-textures      (per-zone + global texture-ref histogram)
//   --info-project-summary       (per-zone BOOTSTRAPPED/PARTIAL/EMPTY)
//
// Returns true if matched; outRc holds the exit code.
bool handleProjectInventory(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
