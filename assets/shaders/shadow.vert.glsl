#version 450

// The caster side of the shadow map, for terrain, buildings and doodads.
//
// The wind here has to be the wind in m2.vert.glsl, vertex for vertex. A tree
// whose shadow sways by a different amount, or with a different profile up its
// trunk, drops a dappled pattern that drifts against its own canopy as the
// phase advances - which from the ground reads as the shadow flickering under
// a tree that looks perfectly still. This shader used to say it matched while
// normalising height against a hardcoded twenty yards at an amplitude of one,
// so the two agreed for a tree exactly twenty yards tall and for nothing else.
// Both sides are handed the same numbers now, from m2_sway.hpp.
layout(push_constant) uniform Push {
    mat4 lightSpaceModel;   // light-space * model, multiplied on the CPU
    vec4 sway;              // xy world origin (phase), z reference height, w amplitude
    ivec4 flags;            // x useTexture, y alphaTest, z foliageSway
    vec4 wind;              // x windTime
} push;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aBoneWeights;
layout(location = 3) in vec4 aBoneIndicesF;

layout(location = 0) out vec2 TexCoord;

void main() {
    vec4 pos = vec4(aPos, 1.0);

    // Wind vertex displacement for foliage - the same three layers, the same
    // constants and the same per-instance phase as m2.vert.glsl.
    if (push.flags.z != 0) {
        vec2 worldRef = push.sway.xy;
        float heightFactor = clamp(pos.z / max(push.sway.z, 0.01), 0.0, 1.0);
        heightFactor *= heightFactor;   // quadratic - the base stays planted
        float amp = push.sway.w * heightFactor;

        // Layer 1: Trunk sway - slow, large amplitude
        float trunkPhase = push.wind.x * 0.8 + dot(worldRef, vec2(0.1, 0.13));
        float trunkSwayX = sin(trunkPhase) * 0.35 * amp;
        float trunkSwayY = cos(trunkPhase * 0.7) * 0.25 * amp;

        // Layer 2: Branch sway - medium frequency, per-branch phase
        float branchPhase = push.wind.x * 1.7 + dot(worldRef, vec2(0.37, 0.71));
        float branchSwayX = sin(branchPhase + pos.y * 0.4) * 0.15 * amp;
        float branchSwayY = cos(branchPhase * 1.1 + pos.x * 0.3) * 0.12 * amp;

        // Layer 3: Leaf flutter - fast, small amplitude, per-vertex
        float leafPhase = push.wind.x * 4.5 + dot(aPos, vec3(1.7, 2.3, 0.9));
        float leafFlutterX = sin(leafPhase) * 0.06 * amp;
        float leafFlutterY = cos(leafPhase * 1.3) * 0.05 * amp;

        pos.x += trunkSwayX + branchSwayX + leafFlutterX;
        pos.y += trunkSwayY + branchSwayY + leafFlutterY;
    }

    TexCoord = aTexCoord;
    gl_Position = push.lightSpaceModel * pos;
}
