#version 450

layout(location = 0) in float vBrightness;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord - vec2(0.5);
    float dist = length(p);
    if (dist > 0.5) discard;
    float alpha = vBrightness * smoothstep(0.5, 0.2, dist);
    outColor = vec4(vec3(0.9, 0.95, 1.0) * vBrightness, alpha);
}
