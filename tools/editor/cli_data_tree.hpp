#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the proprietary-data-tree audit + migration handlers:
//   --migrate-data-tree         --bench-migrate-data-tree
//   --list-data-tree-largest    --export-data-tree-md
//   --info-data-tree            --strip-data-tree
//   --audit-data-tree
//
// All operate on a Blizzard-format extracted Data tree
// (the .m2/.skin/.wmo/.blp/.dbc files).
//
// Returns true if matched; outRc holds the exit code.
bool handleDataTree(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
