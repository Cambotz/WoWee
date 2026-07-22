#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Print the full --help / usage text to stdout. Drop-in
// replacement for the local-static printUsage main.cpp used to
// own; moved here so the 597-line block of printf calls doesn't
// keep main.cpp obese.
//
// argv0 is interpolated into the leading "Usage:" line.
void printUsage(const char* argv0);

} // namespace cli
} // namespace editor
} // namespace wowee
