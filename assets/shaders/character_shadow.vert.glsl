#version 450

layout(push_constant) uniform Push {
    mat4 lightSpaceMatrix;
    mat4 model;
} push;

layout(set = 2, binding = 0) readonly buffer BoneSSBO {
    mat4 bones[];
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aBoneWeights;
layout(location = 2) in ivec4 aBoneIndices;
layout(location = 3) in vec2 aTexCoord;

layout(location = 0) out vec2 TexCoord;

void main() {
    mat4 skinMat = bones[aBoneIndices.x] * aBoneWeights.x
                 + bones[aBoneIndices.y] * aBoneWeights.y
                 + bones[aBoneIndices.z] * aBoneWeights.z
                 + bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
    gl_Position = push.lightSpaceMatrix * push.model * skinnedPos;
}
