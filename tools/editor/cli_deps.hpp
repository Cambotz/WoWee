#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the asset-dependency analysis handlers. All three
// surface the relationship between content references
// (objects.json placements + WOB doodad lists) and on-disk
// model files, supporting --json output for CI pipelines.
//   --list-zone-deps          enumerate external model paths a zone needs
//   --list-project-orphans    on-disk .wom/.wob files no zone references
//   --remove-project-orphans  destructive cleanup (with --dry-run)
//
// Returns true if matched; outRc holds the exit code.
bool handleDeps(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
