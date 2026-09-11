#version 450

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform Push {
    mat4 lightSpaceModel;
    vec4 sway;
    ivec4 flags;            // x useTexture, y alphaTest, z foliageSway
    vec4 wind;
} push;

layout(location = 0) in vec2 TexCoord;

void main() {
    if (push.flags.x != 0) {
        vec4 texColor = textureLod(uTexture, TexCoord, 0.0);
        if (push.flags.y != 0 && texColor.a < 0.5) discard;
    }
}
