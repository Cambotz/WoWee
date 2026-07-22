#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the extracted-data-tree inspection handlers:
//   --info-extract           (per-extension counts + bytes)
//   --info-extract-tree      (per-directory rollup)
//   --info-extract-budget    (proprietary share + open-format gap)
//   --list-missing-sidecars  (find unconverted .m2/.wmo/.blp/.dbc)
//
// All scan a Blizzard-format extracted Data tree directly.
//
// Returns true if matched; outRc holds the exit code.
bool handleExtractInfo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
