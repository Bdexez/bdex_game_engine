// Sandbox: a small scene exercising every layer of the engine.
//
//   - modules chosen at runtime (see sandbox.ini)
//   - ECS components and a system defined by the application (Spin)
//   - transform hierarchy (moons orbiting a pivot)
//   - meshes, textures and materials created through the renderer service
//   - a custom render feature (SkyFeature) plugged into the renderer
//
// Controls: hold the right mouse button to look around, WASD to move,
// Space / Left Ctrl to go up / down, Left Shift to move faster,
// F2 for a screenshot, Escape to quit.

#include "SkyFeature.hpp"

#include <bdex/app/Application.hpp>
#include <bdex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

using namespace bdex;

namespace {

/// Application-defined component: rotates the entity around an axis.
struct Spin {
    Vec3 axis{0.0f, 1.0f, 0.0f};
    f32 speed = 1.0f;  // radians per second
};

/// Application-defined system. Systems are plain functions over ECS views.
void spinSystem(ecs::World& world, f32 deltaSeconds) {
    world.view<scene::Transform, Spin>().each([deltaSeconds](scene::Transform& transform,
                                                            const Spin& spin) {
        transform.rotation =
            glm::normalize(glm::angleAxis(spin.speed * deltaSeconds, spin.axis) * transform.rotation);
    });
}

Vec3 hsvToRgb(f32 hue, f32 saturation, f32 value) {
    const auto channel = [&](f32 offset) {
        const f32 k = std::fmod(offset + hue * 6.0f, 6.0f);
        return value - value * saturation * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f);
    };
    return {channel(5.0f), channel(3.0f), channel(1.0f)};
}

}  // namespace

class Sandbox final : public app::Application {
protected:
    void onConfigure(Config& config) override {
        config.set("window.title", "bdex sandbox");
    }

    Result<> onStart() override {
        // Without a renderer module the sandbox still runs its simulation.
        if (engine().services().has<render::IRenderer>()) {
            if (const auto built = buildRenderResources(); !built) {
                return built;
            }
        } else {
            log::warn("sandbox", "no renderer module loaded: simulation only");
        }
        buildScene();

        engine().scheduler().add(Phase::Update, "sandbox.spin", [this](const FrameTime& time) {
            spinSystem(scene().world(), static_cast<f32>(time.delta));
        });
        return {};
    }

    void onUpdate(const FrameTime& time) override {
        updateCamera(static_cast<f32>(time.delta));
        updateTitle(time);

        if (const auto* keyboard = input(); keyboard != nullptr) {
            if (keyboard->keyPressed(platform::Key::Escape)) {
                engine().requestExit();
            }
            if (keyboard->keyPressed(platform::Key::F2) &&
                engine().services().has<render::IRenderer>()) {
                renderer().requestScreenshot(std::format("screenshot_{}.png", time.frame));
            }
        }
    }

private:
    Result<> buildRenderResources() {
        render::IRenderer& gfx = renderer();

        if (const auto added = gfx.addFeature(std::make_unique<sandbox::SkyFeature>()); !added) {
            return added;
        }

        cubeMesh_ = gfx.createMesh(assets::makeCube(1.0f));
        sphereMesh_ = gfx.createMesh(assets::makeSphere(0.5f, 48, 24));
        planeMesh_ = gfx.createMesh(assets::makePlane(40.0f, 20.0f));

        const render::TextureHandle checker = gfx.createTexture(
            assets::makeCheckerboard(256, 8, {235, 235, 235, 255}, {120, 124, 130, 255}));
        const render::TextureHandle crate = gfx.createTexture(
            assets::makeCheckerboard(64, 4, {255, 255, 255, 255}, {200, 200, 200, 255}));

        render::MaterialDesc ground;
        ground.baseColor = Vec4(0.55f, 0.6f, 0.55f, 1.0f);
        ground.albedo = checker;
        ground.specular = 0.05f;
        groundMaterial_ = gfx.createMaterial(ground);

        render::MaterialDesc polished;  // no texture: plain base color
        polished.baseColor = Vec4(0.95f, 0.95f, 1.0f, 1.0f);
        polished.specular = 0.9f;
        polished.shininess = 96.0f;
        sphereMaterial_ = gfx.createMaterial(polished);

        for (u32 i = 0; i < kCubeCount; ++i) {
            render::MaterialDesc painted;
            painted.baseColor = Vec4(hsvToRgb(static_cast<f32>(i) / kCubeCount, 0.65f, 0.95f), 1.0f);
            painted.albedo = crate;
            painted.specular = 0.4f;
            cubeMaterials_[i] = gfx.createMaterial(painted);
        }
        return {};
    }

    void buildScene() {
        scene::Scene& level = scene();
        ecs::World& world = level.world();
        const f32 pi = std::numbers::pi_v<f32>;

        // Sun
        const ecs::Entity sun = level.createEntity("Sun");
        world.emplace<render::DirectionalLight>(
            sun, render::DirectionalLight{.direction = Vec3(-0.45f, -0.8f, -0.35f),
                                          .color = Vec3(1.0f, 0.96f, 0.9f),
                                          .intensity = 1.1f,
                                          .ambient = Vec3(0.16f, 0.19f, 0.25f)});

        // Camera
        camera_ = level.createEntity("Camera");
        world.emplace<render::Camera>(camera_);
        world.get<scene::Transform>(camera_).position = Vec3(0.0f, 3.2f, 9.5f);
        cameraPitch_ = -0.26f;
        applyCameraRotation();

        // Ground
        const ecs::Entity ground = level.createEntity("Ground");
        world.emplace<render::MeshRenderer>(ground, planeMesh_, groundMaterial_);

        // Centerpiece
        const ecs::Entity sphere = level.createEntity("Sphere");
        world.get<scene::Transform>(sphere).position = Vec3(0.0f, 1.4f, 0.0f);
        world.get<scene::Transform>(sphere).scale = Vec3(2.2f);
        world.emplace<render::MeshRenderer>(sphere, sphereMesh_, sphereMaterial_);

        // A ring of spinning cubes
        for (u32 i = 0; i < kCubeCount; ++i) {
            const f32 angle = 2.0f * pi * static_cast<f32>(i) / kCubeCount;
            const ecs::Entity cube = level.createEntity(std::format("Cube {}", i));
            scene::Transform& transform = world.get<scene::Transform>(cube);
            transform.position = Vec3(std::cos(angle) * 4.5f, 0.75f, std::sin(angle) * 4.5f);
            transform.rotation = glm::angleAxis(angle, Vec3(0.0f, 1.0f, 0.0f));
            world.emplace<render::MeshRenderer>(cube, cubeMesh_, cubeMaterials_[i]);
            world.emplace<Spin>(cube, glm::normalize(Vec3(0.3f, 1.0f, 0.2f * static_cast<f32>(i))),
                                0.6f + 0.15f * static_cast<f32>(i));
        }

        // Hierarchy: an invisible pivot spins above the sphere and carries
        // two moons. Only the pivot is animated; the moons follow through
        // their Parent link.
        const ecs::Entity pivot = level.createEntity("Moon pivot");
        world.get<scene::Transform>(pivot).position = Vec3(0.0f, 3.4f, 0.0f);
        world.emplace<Spin>(pivot, Vec3(0.0f, 1.0f, 0.0f), 0.9f);
        for (u32 i = 0; i < 2; ++i) {
            const ecs::Entity moon = level.createEntity(std::format("Moon {}", i));
            scene::Transform& transform = world.get<scene::Transform>(moon);
            transform.position = Vec3(i == 0 ? 2.0f : -2.0f, 0.0f, 0.0f);
            transform.scale = Vec3(0.45f);
            world.emplace<scene::Parent>(moon, pivot);
            world.emplace<render::MeshRenderer>(moon, i == 0 ? sphereMesh_ : cubeMesh_,
                                                cubeMaterials_[i * 4]);
            world.emplace<Spin>(moon, Vec3(1.0f, 0.0f, 0.0f), 2.0f);
        }

        log::info("sandbox", "scene ready: {} entities", world.entityCount());
    }

    void applyCameraRotation() {
        scene().world().get<scene::Transform>(camera_).rotation =
            glm::angleAxis(cameraYaw_, Vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(cameraPitch_, Vec3(1.0f, 0.0f, 0.0f));
    }

    void updateCamera(f32 deltaSeconds) {
        const platform::InputState* controls = input();
        if (controls == nullptr) {
            return;
        }
        using platform::Key;

        // Mouse look while the right button is held.
        const bool looking = controls->mouseButtonDown(platform::MouseButton::Right);
        if (looking != looking_) {
            looking_ = looking;
            if (platform::IWindow* target = window()) {
                target->setCursorCaptured(looking);
            }
        }
        if (looking_) {
            const Vec2 delta = controls->mouseDelta();
            const f32 limit = std::numbers::pi_v<f32> * 0.49f;
            cameraYaw_ -= delta.x * 0.0025f;
            cameraPitch_ = std::clamp(cameraPitch_ - delta.y * 0.0025f, -limit, limit);
            applyCameraRotation();
        }

        scene::Transform& transform = scene().world().get<scene::Transform>(camera_);
        Vec3 direction(0.0f);
        if (controls->keyDown(Key::W)) direction += transform.forward();
        if (controls->keyDown(Key::S)) direction -= transform.forward();
        if (controls->keyDown(Key::D)) direction += transform.right();
        if (controls->keyDown(Key::A)) direction -= transform.right();
        if (controls->keyDown(Key::Space)) direction += Vec3(0.0f, 1.0f, 0.0f);
        if (controls->keyDown(Key::LeftControl)) direction -= Vec3(0.0f, 1.0f, 0.0f);
        if (glm::length(direction) > 0.0f) {
            const f32 speed = controls->keyDown(Key::LeftShift) ? 14.0f : 5.0f;
            transform.position += glm::normalize(direction) * speed * deltaSeconds;
        }
    }

    void updateTitle(const FrameTime& time) {
        ++framesSinceTitle_;
        secondsSinceTitle_ += time.delta;
        if (secondsSinceTitle_ < 0.5) {
            return;
        }
        if (platform::IWindow* target = window()) {
            const f64 fps = framesSinceTitle_ / secondsSinceTitle_;
            target->setTitle(std::format("bdex sandbox - {:.0f} fps ({:.2f} ms)", fps, 1000.0 / fps));
        }
        framesSinceTitle_ = 0;
        secondsSinceTitle_ = 0.0;
    }

    static constexpr u32 kCubeCount = 8;

    render::MeshHandle cubeMesh_;
    render::MeshHandle sphereMesh_;
    render::MeshHandle planeMesh_;
    render::MaterialHandle groundMaterial_;
    render::MaterialHandle sphereMaterial_;
    std::array<render::MaterialHandle, kCubeCount> cubeMaterials_{};

    ecs::Entity camera_;
    f32 cameraYaw_ = 0.0f;
    f32 cameraPitch_ = 0.0f;
    bool looking_ = false;

    u32 framesSinceTitle_ = 0;
    f64 secondsSinceTitle_ = 0.0;
};

BDEX_MAIN(Sandbox)
