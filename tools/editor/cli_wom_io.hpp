#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the WOM <-> interchange-format handlers. WOM is our
// open M2 replacement; these export it to / import it from the
// formats every other 3D tool understands:
//   --export-obj     WOM -> Wavefront OBJ (universal text format)
//   --export-glb     WOM -> glTF 2.0 binary (browsers, Three.js)
//   --export-stl     WOM -> ASCII STL (slicers / 3D printers)
//   --import-stl     ASCII STL -> WOM (round-trip from CAD tools)
//   --import-obj     Wavefront OBJ -> WOM (round-trip from Blender etc.)
//
// Returns true if matched; outRc holds the exit code.
bool handleWomIo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
