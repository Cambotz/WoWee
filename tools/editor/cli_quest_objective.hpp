#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the quest / quest-objective mutation handlers.
// All three operate on a zone's quests.json:
//   --add-quest                append a new quest (title + opt fields)
//   --add-quest-objective      append an objective to an existing quest
//   --remove-quest-objective   remove an objective by index
//
// Reward and clone-quest operations live in their own
// modules (cli_quest_reward / cli_clone) so the per-quest
// objective logic stays focused.
//
// Returns true if matched; outRc holds the exit code.
bool handleQuestObjective(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
