#pragma once

#include <string>

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the single-file format-conversion handlers. These are
// the source-of-truth implementations; the batch wrappers in
// cli_convert.cpp shell out to wowee_editor with these flags.
//   --convert-m2          M2 → WOM (uses asset manager + dataPath)
//   --convert-wmo         WMO → WOB (uses asset manager + dataPath)
//   --convert-dbc-json    DBC → JSON sidecar
//   --convert-json-dbc    JSON sidecar → binary DBC
//   --convert-blp-png     BLP → PNG
//
// dataPath is mutated to "Data" if empty so the M2/WMO handlers
// have a default location to look in.
//
// Returns true if matched; outRc holds the exit code.
bool handleConvertSingle(int& i, int argc, char** argv,
                         std::string& dataPath, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
