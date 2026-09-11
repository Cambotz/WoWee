#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace wowee::rendering {

/// A world pass an ablation run can switch off for a few seconds.
enum class AblationPass {
    None = 0,
    Terrain,
    Grass,
    WMO,
    M2,
    Characters,
    Sky,
    Shadows,
};

const char* ablationPassName(AblationPass pass);

/// What a pass costs, measured by taking it away.
///
/// The GPU's own timestamps cannot answer that on this Mac. MoltenVK resolves
/// a timestamp against the Metal encoder that contains it, so every mark
/// inside one render pass reads the same clock - the first mark in the scene
/// pass absorbs the whole pass and the six after it report two microseconds
/// apiece. A profile taken that way said terrain cost 14ms and that the
/// characters, the buildings, the water and every doodad in Stormwind came to
/// twenty microseconds between them. The three passes that do read sensibly -
/// shadows, post-process, interface - are the three that have a render pass to
/// themselves, which is the tell.
///
/// So it is measured from the other side: run with a pass switched off and see
/// what the frame does. One run walks the list, holds each phase long enough
/// for the average to settle, and returns to the baseline at the end so drift
/// over the run is visible rather than charged to the last pass measured.
///
/// The phase clock is the frames themselves, not a wall clock: a phase that
/// renders nothing runs faster and would otherwise be sampled for fewer frames
/// than the one it is compared against.
class PassAblation {
public:
    /// `phaseMs` is how long each phase is sampled for, `warmupMs` how much of
    /// the front of it is thrown away - a phase's first frames still carry the
    /// previous phase's resident set and its pipeline warm-up.
    PassAblation(double phaseMs = 4000.0, double warmupMs = 750.0);

    /// One frame's wall time, from the top of a frame to the top of the next.
    void frame(double frameMs);

    /// Is this pass switched off for the phase now running?
    bool skip(AblationPass pass) const;

    /// Still walking the list.
    bool running() const { return phase_ < phaseCount(); }

    /// Which phase is running, for a caller that wants to say so.
    AblationPass current() const;

    /// The table. Empty until every phase has been held.
    std::string report() const;

    /// Number of phases in a run, the two baselines included.
    static std::size_t phaseCount();

private:
    struct Sample {
        int frames = 0;
        double totalMs = 0.0;
    };

    double phaseMs_;
    double warmupMs_;
    std::size_t phase_ = 0;
    double elapsedMs_ = 0.0;
    std::vector<Sample> samples_;
};

}  // namespace wowee::rendering
