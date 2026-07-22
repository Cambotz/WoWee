#version 450

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 shadowParams;
};

layout(push_constant) uniform Push {
    float time;
    float intensity;
} push;

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aBrightness;
layout(location = 2) in float aTwinklePhase;

layout(location = 0) out float vBrightness;

void main() {
    mat4 rotView = mat4(mat3(view));
    float twinkle = 0.7 + 0.3 * sin(push.time * 1.5 + aTwinklePhase);
    vBrightness = aBrightness * twinkle * push.intensity;
    gl_PointSize = mix(2.0, 4.0, aBrightness);
    gl_Position = projection * rotView * vec4(aPos, 1.0);
}
