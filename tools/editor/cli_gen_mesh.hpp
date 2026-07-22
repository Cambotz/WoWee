#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the procedural composite-prop mesh generators that
// have been moved out of main.cpp:
//   --gen-mesh-rock     --gen-mesh-pillar
//   --gen-mesh-bridge   --gen-mesh-tower
//   --gen-mesh-house    --gen-mesh-fountain
//   --gen-mesh-statue   --gen-mesh-altar
//   --gen-mesh-portal   --gen-mesh-archway
//   --gen-mesh-barrel   --gen-mesh-chest
//
// Other mesh handlers (the `--gen-mesh <type>` dispatcher,
// gen-mesh-fence/-tree/-grid/-stairs/-disc/-tube/-capsule/-arch/
// -pyramid/-from-heightmap/-textured) still live in main.cpp
// and may be migrated in subsequent batches.
//
// Returns true if matched; outRc holds the exit code.
bool handleGenMesh(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
