#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the --info-tilemap project-wide tile-grid handler.
// Renders the WoW 64x64 ADT grid showing which tiles are
// claimed by which zones, with collision detection (multiple
// zones claiming the same tile). Useful for spotting tile-
// coord overlaps before two zones try to ship conflicting
// content. Supports --json for machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleTilemap(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
