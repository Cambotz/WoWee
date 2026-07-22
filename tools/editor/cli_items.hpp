#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the items.json read-only inspection handlers:
//   --list-items            --info-item
//   --validate-items        --validate-project-items
//   --info-project-items
//
// Item editing commands (--add-item, --set-item, --remove-item)
// stay in main.cpp since they share state with quest reward
// editing logic.
//
// Returns true if matched; outRc holds the exit code.
bool handleItems(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
