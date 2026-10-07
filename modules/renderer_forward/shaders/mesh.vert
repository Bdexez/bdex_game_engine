#version 450
#extension GL_GOOGLE_include_directive : require
#include "bdex/frame_uniforms.glsl"

layout(push_constant) uniform ObjectConstants {
    mat4 model;
    vec4 baseColor;
    vec4 params;  // x: specular strength, y: shininess
} object;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUv;

void main() {
    vec4 worldPosition = object.model * vec4(inPosition, 1.0);
    outWorldPosition = worldPosition.xyz;
    // Inverse-transpose keeps normals perpendicular under non-uniform scale.
    outWorldNormal = mat3(transpose(inverse(object.model))) * inNormal;
    outUv = inUv;
    gl_Position = frame.viewProjection * worldPosition;
}
