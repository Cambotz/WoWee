#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the WoWee Content Pack (.wcp) handlers:
//   --list-wcp           --info-wcp
//   --info-pack-budget   --info-pack-tree
//   --pack-wcp           --unpack-wcp
//
// All defer to wowee::editor::ContentPacker for actual pack
// I/O; the handlers just parse args and format output.
//
// Returns true if matched; outRc holds the exit code.
bool handlePack(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
