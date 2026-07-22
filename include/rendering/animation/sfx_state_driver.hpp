#pragma once

#include <cstdint>

namespace wowee {
namespace rendering {

class Renderer;
class FootstepDriver;

// ============================================================================
// SfxStateDriver — extracted from AnimationController
//
// Tracks state transitions for activity SFX (jump, landing, swim) and
// mount ambient sounds.
// ============================================================================
class SfxStateDriver {
public:
    SfxStateDriver() = default;

    /// Track state transitions and trigger appropriate SFX.
    void update(float deltaTime, Renderer* renderer,
                bool mounted, bool taxiFlight,
                FootstepDriver& footstepDriver);

private:
    bool initialized_ = false;
    bool prevGrounded_ = true;
    bool prevJumping_ = false;
    bool prevFalling_ = false;
    bool prevSwimming_ = false;
};

} // namespace rendering
} // namespace wowee
