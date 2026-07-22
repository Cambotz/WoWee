#pragma once

#include <cstddef>

namespace wowee {
namespace editor {
namespace cli {

// Flat list of every CLI flag that takes one or more positional
// arguments. main.cpp uses this for the early "missing argument"
// detector that bails out with a helpful message instead of
// silently dropping into the GUI; cli_introspect.cpp's
// --validate-cli-help uses it for the self-check that asserts
// every entry is documented in printUsage.
//
// kArgRequired is null-terminated for backwards compatibility
// with range-for loops that don't use the size; kArgRequiredSize
// is the count excluding the terminator.
extern const char* const kArgRequired[];
extern const std::size_t kArgRequiredSize;

} // namespace cli
} // namespace editor
} // namespace wowee
