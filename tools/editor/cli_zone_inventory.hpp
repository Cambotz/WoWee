#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the four per-zone inventory handlers:
//   --list-zone-meshes   (WOMs with vert/tri/bone/anim/batch counts)
//   --list-zone-audio    (WAVs with sample-rate/duration)
//   --list-zone-textures (texture refs across every WOM)
//   --info-zone-summary  (one-glance BOOTSTRAPPED/PARTIAL/EMPTY)
//
// Returns true if matched; outRc holds the exit code.
bool handleZoneInventory(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
