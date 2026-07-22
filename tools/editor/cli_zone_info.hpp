#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the zone & project metadata inspection handlers:
//   --info-zone              (single zone.json print)
//   --info-zone-overview     (high-level zone digest)
//   --info-project-overview  (per-zone summary table for a project)
//
// All read zone.json via wowee::editor::ZoneManifest::loadFromFile.
//
// Returns true if matched; outRc holds the exit code.
bool handleZoneInfo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
