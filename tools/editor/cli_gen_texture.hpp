#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the procedural texture generator handlers that have
// been moved out of main.cpp. Currently the 7 newer Worley/
// noise-based generators:
//   --gen-texture-cobble  --gen-texture-marble
//   --gen-texture-metal   --gen-texture-leather
//   --gen-texture-sand    --gen-texture-snow
//   --gen-texture-lava
//
// Older simpler generators (gradient/noise/radial/stripes/dots/
// rings/checker/brick/wood/grass/fabric) still live in main.cpp
// and will be migrated in subsequent batches.
//
// Returns true if matched; outRc holds the exit code.
bool handleGenTexture(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
