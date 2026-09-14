#pragma once

/// Writing a built asset set out as one file somebody else can install.
///
/// Zip, because a pack is a thing people send each other and a zip opens on
/// every desktop without installing something to open it with. Deflate comes
/// from zlib, which this program already links for CASC, so the format costs
/// no dependency of its own.
///
/// Level 6 rather than 9: the tree is almost entirely BLP and M2, both already
/// compressed, and the levels above 6 spend minutes to win single figures.

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace wowee::assets {

struct PackResult {
    std::size_t files = 0;
    std::size_t rawBytes = 0;
    std::size_t packedBytes = 0;
    bool ok = false;
    std::string error;
};

/// Write everything under `sourceDir` into `destZip`, beneath a Data/ prefix,
/// with a pack.json describing it.
///
/// `progress` is called with (done, total) every so often; `cancel` is polled
/// between files.
PackResult writePack(const std::string& sourceDir, const std::string& destZip,
                     const std::string& name,
                     const std::function<void(std::size_t, std::size_t)>& progress,
                     const std::atomic<bool>& cancel);

}  // namespace wowee::assets
