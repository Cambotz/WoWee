#pragma once

#include <algorithm>

namespace wowee::rendering {

/// How a model bends in the wind, as both passes that draw it must agree.
///
/// The main pass and the shadow pass each displace foliage vertices, and they
/// have to displace them identically: a tree whose shadow sways by a different
/// amount, or with a different profile up its trunk, drops a dappled pattern
/// that drifts against its own canopy as the wind phase advances. From the
/// ground that reads as the shadow flickering under a tree that looks fine.
///
/// They did diverge. shadow.vert.glsl said "matches m2.vert.glsl" while
/// normalising height against a hardcoded twenty yards at an amplitude of one,
/// where the main pass uses the model's own height and its own throw. The two
/// agreed for a tree exactly twenty yards tall and for nothing else.
///
/// So neither pass computes this any more; both ask here.
struct M2Sway {
    /// What the vertex shaders switch on: -1 sky, 0 none, 1 foliage,
    /// 2 ground clutter, 3 hanging cloth.
    int mode = 0;
    /// The height the bend is normalised against, in model space.
    float refHeight = 20.0f;
    /// Amplitude scale; 1.0 is the tree-sized default.
    float amp = 1.0f;
    /// The model's own height, for the brush a player pushes through it with.
    float plantHeight = 0.0f;
};

/// The sway a model gets from what it is and how big it is.
///
/// `boundMinZ`/`boundMaxZ` are the model's own bounds. Height is measured from
/// the model's base rather than from its origin: a few detail doodads sit with
/// geometry below z=0.
inline M2Sway m2SwayFor(bool sky, bool hangingCloth, bool windFoliage, bool groundDetail,
                        float boundMinZ, float boundMaxZ) {
    M2Sway out;
    // The model's own height, whatever it is made of. It used to be filled in
    // only on the paths that sway, so anything else reached the shader with
    // zero and ModelHeight came out as a flat 1.0 - which is no use to a
    // fragment that wants to know how far up the model it is. A fire is not
    // foliage and still has a top.
    out.plantHeight = std::max(boundMaxZ - std::min(boundMinZ, 0.0f), 0.05f);
    if (sky) {
        out.mode = -1;
        return out;
    }
    if (hangingCloth) {
        // Held at the top and free at the hem, so the shader is given the top
        // rather than a height to normalise against. The throw is a twentieth
        // of the cloth's own drop: a banner indoors breathes, it does not flap.
        const float span = std::max(boundMaxZ - boundMinZ, 0.05f);
        out.mode = 3;
        out.refHeight = boundMaxZ;
        out.plantHeight = span;
        out.amp = span * 0.05f;
        return out;
    }
    if (!windFoliage) {
        out.mode = 0;
        return out;
    }
    out.mode = groundDetail ? 2 : 1;

    const float height = std::max(boundMaxZ - std::min(boundMinZ, 0.0f), 0.05f);
    out.refHeight = height;
    out.plantHeight = height;

    // How far the tip travels as a fraction of the plant's own height: about a
    // tenth for grass, a fiftieth for a tree, and the blend between them for
    // everything in the middle. 0.35 is the trunk layer's throw in the shader,
    // so dividing by it turns a fraction back into that scale.
    const float t = std::clamp((height - 1.0f) / 19.0f, 0.0f, 1.0f);
    const float relativeThrow = 0.105f + (0.0175f - 0.105f) * t;
    out.amp = relativeThrow * height / 0.35f;
    return out;
}

}  // namespace wowee::rendering
