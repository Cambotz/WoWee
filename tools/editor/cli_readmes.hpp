#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the two README auto-generators:
//   --gen-zone-readme    -> README.md inside a zone
//   --gen-project-readme -> PROJECT.md at a project root
//
// Returns true if matched; outRc holds the exit code.
bool handleReadmes(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
