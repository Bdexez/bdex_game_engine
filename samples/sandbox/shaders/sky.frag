#version 450
#extension GL_GOOGLE_include_directive : require
#include "bdex/frame_uniforms.glsl"

layout(push_constant) uniform SkyConstants {
    vec4 horizonColor;
    vec4 zenithColor;
    vec4 groundColor;
} sky;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

void main() {
    // Rebuild the view ray of this pixel from the inverse view-projection.
    vec4 farPoint = frame.inverseViewProjection * vec4(inNdc, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - frame.cameraPosition.xyz);

    float height = direction.y;
    vec3 color = height >= 0.0
        ? mix(sky.horizonColor.rgb, sky.zenithColor.rgb, pow(height, 0.5))
        : mix(sky.horizonColor.rgb, sky.groundColor.rgb, pow(-height, 0.35));

    // Sun disc and glow around the light direction.
    float sunAmount = max(dot(direction, normalize(-frame.lightDirection.xyz)), 0.0);
    color += frame.lightColor.rgb * (pow(sunAmount, 900.0) * 4.0 + pow(sunAmount, 24.0) * 0.18);

    outColor = vec4(color, 1.0);
}
