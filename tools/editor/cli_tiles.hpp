#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the per-tile zone-manifest handlers. Zones can span
// multiple ADT tiles; these manage that list:
//   --add-tile      append a new tile (creates blank WHM/WOT pair)
//   --remove-tile   drop a tile entry + delete its WHM/WOT/WOC files
//   --list-tiles    print the manifest's tile list (with --json mode)
//
// Returns true if matched; outRc holds the exit code.
bool handleTiles(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
