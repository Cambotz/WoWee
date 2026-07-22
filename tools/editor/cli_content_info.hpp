#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the creature/object/quest content inspection
// handlers (16 in this group):
//
//   --info-creatures              --info-creatures-by-faction
//   --info-creatures-by-level     --info-objects-by-path
//   --info-objects-by-type        --info-quests-by-level
//   --info-quests-by-xp           --list-creatures
//   --list-objects                --list-quests
//   --list-quest-objectives       --list-quest-rewards
//   --info-quest-graph-stats      --info-creature
//   --info-quest                  --info-object
//
// All read JSON sidecars (zone-spawns.json, zone-objects.json,
// zone-quests.json) via wowee::editor::{NpcSpawner, ObjectPlacer,
// QuestEditor} loaders.
//
// Returns true if matched; outRc holds the exit code.
bool handleContentInfo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
