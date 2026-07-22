#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the GLB introspection / validation handlers. GLB is
// our open replacement for proprietary M2/WMO bake outputs;
// these handlers help debug what the baker produced and verify
// the result is glTF-2.0 compliant.
//   --validate-glb / --info-glb   shared parser, different verdict
//   --info-glb-tree               tree-style scene/node/mesh dump
//   --info-glb-bytes              per-section + per-bufferView size table
//   --check-glb-bounds            cross-check accessor min/max vs BIN data
//
// All four support an optional trailing `--json` flag for
// machine-readable output.
//
// Returns true if matched; outRc holds the exit code.
bool handleGlbInspect(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
