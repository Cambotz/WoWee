#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the batch format-conversion handlers:
//   --convert-m2-batch    (M2 → WOM)
//   --convert-wmo-batch   (WMO → WOB)
//   --convert-blp-batch   (BLP → PNG)
//   --convert-dbc-batch   (DBC → JSON)
//
// Each fans out to its single-file --convert-* counterpart via
// subprocess so the existing per-file logic stays the source of
// truth.
//
// Returns true if matched; outRc holds the exit code.
bool handleConvert(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
