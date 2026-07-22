#version 450

// Full-screen color overlay (e.g. underwater tint).
// Uses postprocess.vert.glsl as vertex shader (fullscreen triangle, no vertex input).

layout(push_constant) uniform Push {
    vec4 color; // rgb = tint color, a = opacity
} push;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = push.color;
}
