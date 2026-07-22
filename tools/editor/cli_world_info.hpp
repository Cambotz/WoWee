#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the world-asset inspectors. WOB / WOT (paired
// with WHM) / WOC are our open replacements for proprietary
// WMO / ADT-heightmap / ADT-collision data; these print a
// quick structural summary (groups / portals / chunks /
// triangles / bounds) without paying the full deserialization
// cost a viewer would.
//   --info-wob   building summary (groups, portals, doodads)
//   --info-wot   terrain tile summary (chunk counts, height range)
//   --info-woc   collision mesh summary (tris, walkable %, bounds)
//
// All three support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleWorldInfo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
