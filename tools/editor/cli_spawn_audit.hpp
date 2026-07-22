#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the NPC spawn / object placer audit + ground-snap
// handlers (8 in this group):
//   --snap-zone-to-ground       --snap-project-to-ground
//   --audit-zone-spawns         --audit-project-spawns
//   --list-zone-spawns          --list-project-spawns
//   --diff-zone-spawns          --info-spawn
//
// All operate on creatures.json + objects.json sidecars and
// the WHM terrain heightfield via WoweeTerrainLoader.
//
// Returns true if matched; outRc holds the exit code.
bool handleSpawnAudit(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
