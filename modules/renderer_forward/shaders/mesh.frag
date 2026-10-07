#version 450
#extension GL_GOOGLE_include_directive : require
#include "bdex/frame_uniforms.glsl"

layout(push_constant) uniform ObjectConstants {
    mat4 model;
    vec4 baseColor;
    vec4 params;  // x: specular strength, y: shininess
} object;

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 albedo = texture(albedoTexture, inUv) * object.baseColor;

    vec3 normal = normalize(inWorldNormal);
    vec3 toLight = normalize(-frame.lightDirection.xyz);
    vec3 toCamera = normalize(frame.cameraPosition.xyz - inWorldPosition);
    vec3 halfVector = normalize(toLight + toCamera);

    // Blinn-Phong, in linear space (the render target is sRGB-encoded).
    float diffuse = max(dot(normal, toLight), 0.0);
    float specular = diffuse > 0.0
        ? pow(max(dot(normal, halfVector), 0.0), object.params.y) * object.params.x
        : 0.0;

    vec3 color = albedo.rgb * (frame.ambientColor.rgb + frame.lightColor.rgb * diffuse)
               + frame.lightColor.rgb * specular;
    outColor = vec4(color, albedo.a);
}
