#include <bdex/render/Renderer.hpp>

#include <algorithm>

namespace bdex::render {

CameraData makeCameraData(const Mat4& cameraWorld, const Camera& camera, f32 aspectRatio) {
    CameraData data;
    data.view = glm::inverse(cameraWorld);
    data.projection = glm::perspective(camera.verticalFov, std::max(aspectRatio, 1e-4f),
                                       camera.nearPlane, camera.farPlane);
    // Clip space has Y pointing down (Vulkan convention, which the RHI
    // adopts); flipping here keeps +Y up in the world and counter-clockwise
    // front faces on screen.
    data.projection[1][1] *= -1.0f;
    data.viewProjection = data.projection * data.view;
    data.position = Vec3(cameraWorld[3]);
    return data;
}

}  // namespace bdex::render
