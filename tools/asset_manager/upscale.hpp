#pragma once

/// Choosing which textures to resample, and doing it.
///
/// Selection is by model rather than by filename. Each M2 is read for its own
/// texture list and that list is taken whole, so a trunk sheet named Bark03.blp
/// comes along with the tree that uses it - which a sweep over texture names
/// would have missed. The exception is a sheet many models draw: one of those
/// belongs to the whole world rather than to this tree, and upscaling it
/// changes every creature that touches it too.
///
/// Whether a model is foliage is asked of the same classifier the client uses,
/// so the two cannot drift apart about what a tree is.

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace wowee::assets {

struct UpscalePlan {
    std::vector<std::string> textures;   ///< absolute paths of BLPs to resample
    std::size_t foliageModels = 0;
    std::size_t sharedSkipped = 0;       ///< sheets too widely used to touch
};

/// Walk an extracted expansion and decide what to resample. Reads nothing but
/// model headers, so it is quick enough to show before committing.
UpscalePlan planUpscale(const std::string& expansionDir, int sharedLimit = 8);

struct UpscaleResult {
    std::size_t written = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
};

/// Resample each chosen texture into a .dds sidecar beside it.
///
/// `say` receives one line per interesting event; `cancel` is polled between
/// textures so a window can stop it.
UpscaleResult runUpscale(const UpscalePlan& plan, int scale,
                         const std::function<void(const std::string&)>& say,
                         const std::atomic<bool>& cancel);

}  // namespace wowee::assets
