#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the open-format validation + project audit handlers:
//   --validate                    --validate-wom
//   --validate-wob                --validate-woc
//   --validate-whm                --validate-all
//   --validate-project            --validate-project-open-only
//   --audit-project               --bench-audit-project
//   --bench-validate-project
//
// Returns true if matched; outRc holds the exit code.
bool handleFormatValidate(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
