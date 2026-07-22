#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the report-export handlers (md / csv / html / sha256 /
// graphviz) for zone & project audits:
//   --export-zone-summary-md       --export-zone-csv
//   --export-zone-checksum         --export-project-checksum
//   --validate-project-checksum    --export-zone-html
//   --export-project-html          --export-project-md
//   --export-quest-graph
//
// Returns true if matched; outRc holds the exit code.
bool handleExport(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
