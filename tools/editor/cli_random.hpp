#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the random-* / gen-random-* zone-population handlers.
// All four use a seeded LCG so re-runs reproduce the same
// content; they're intended for playtest scenarios where you
// want bulk-populated zones without hand-typing every spawn.
//   --random-populate-zone   add N creatures + M objects to a zone
//   --random-populate-items  add N item records to items.json
//   --gen-random-zone        scaffold + populate + items in one shot
//   --gen-random-project     spawn N gen-random-zones in a raster
//
// Returns true if matched; outRc holds the exit code.
bool handleRandom(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
