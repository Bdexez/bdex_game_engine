#include <bdex/render/Renderer.hpp>
#include <bdex/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <numbers>

using namespace bdex;
using namespace bdex::scene;

namespace {

Vec3 worldPosition(ecs::World& world, ecs::Entity entity) {
    return Vec3(world.get<WorldTransform>(entity).matrix[3]);
}

void expectNear(const Vec3& actual, const Vec3& expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

}  // namespace

TEST(Scene, CreateEntityAddsStandardComponents) {
    Scene scene("test");
    const ecs::Entity entity = scene.createEntity("Player");
    EXPECT_EQ(scene.world().get<Name>(entity).value, "Player");
    EXPECT_TRUE((scene.world().has<Transform, WorldTransform>(entity)));
}

TEST(SceneManager, FirstSceneBecomesActive) {
    SceneManager scenes;
    EXPECT_EQ(scenes.active(), nullptr);
    Scene& first = scenes.create("first");
    Scene& second = scenes.create("second");
    EXPECT_EQ(scenes.active(), &first);

    scenes.setActive(&second);
    scenes.destroy(second);
    EXPECT_EQ(scenes.active(), nullptr);
    EXPECT_EQ(scenes.sceneCount(), 1u);
}

TEST(Transform, LocalMatrixAppliesScaleThenRotationThenTranslation) {
    Transform transform;
    transform.position = Vec3(10.0f, 0.0f, 0.0f);
    transform.rotation = glm::angleAxis(std::numbers::pi_v<f32> * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
    transform.scale = Vec3(2.0f);

    // (1, 0, 0) -> scaled to (2, 0, 0) -> rotated 90 deg around Y to (0, 0, -2) -> translated.
    const Vec3 point = Vec3(transform.localMatrix() * Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    expectNear(point, Vec3(10.0f, 0.0f, -2.0f));

    expectNear(transform.forward(), Vec3(-1.0f, 0.0f, 0.0f));
    expectNear(transform.up(), Vec3(0.0f, 1.0f, 0.0f));
}

TEST(Hierarchy, ChildrenFollowTheirParents) {
    Scene scene("test");
    ecs::World& world = scene.world();

    // Created child-first on purpose: order must not matter.
    const ecs::Entity grandchild = scene.createEntity("grandchild");
    const ecs::Entity child = scene.createEntity("child");
    const ecs::Entity root = scene.createEntity("root");
    world.emplace<Parent>(child, root);
    world.emplace<Parent>(grandchild, child);

    world.get<Transform>(root).position = Vec3(0.0f, 5.0f, 0.0f);
    world.get<Transform>(root).rotation =
        glm::angleAxis(std::numbers::pi_v<f32> * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
    world.get<Transform>(child).position = Vec3(1.0f, 0.0f, 0.0f);
    world.get<Transform>(grandchild).position = Vec3(1.0f, 0.0f, 0.0f);

    updateWorldTransforms(world);

    expectNear(worldPosition(world, root), Vec3(0.0f, 5.0f, 0.0f));
    // +X in the root's rotated frame is -Z in the world.
    expectNear(worldPosition(world, child), Vec3(0.0f, 5.0f, -1.0f));
    expectNear(worldPosition(world, grandchild), Vec3(0.0f, 5.0f, -2.0f));
}

TEST(Hierarchy, BrokenAndCyclicLinksDoNotHang) {
    Scene scene("test");
    ecs::World& world = scene.world();

    const ecs::Entity orphan = scene.createEntity("orphan");
    const ecs::Entity gone = scene.createEntity("gone");
    world.emplace<Parent>(orphan, gone);
    world.destroy(gone);
    world.get<Transform>(orphan).position = Vec3(3.0f, 0.0f, 0.0f);

    const ecs::Entity a = scene.createEntity("a");
    const ecs::Entity b = scene.createEntity("b");
    world.emplace<Parent>(a, b);
    world.emplace<Parent>(b, a);

    updateWorldTransforms(world);  // must terminate
    expectNear(worldPosition(world, orphan), Vec3(3.0f, 0.0f, 0.0f));
}

TEST(Camera, LooksDownNegativeZWithYUp) {
    Transform cameraTransform;
    cameraTransform.position = Vec3(0.0f, 0.0f, 5.0f);
    const render::CameraData camera =
        render::makeCameraData(cameraTransform.localMatrix(), render::Camera{}, 16.0f / 9.0f);

    expectNear(camera.position, Vec3(0.0f, 0.0f, 5.0f));

    const auto project = [&](const Vec3& world) {
        const Vec4 clip = camera.viewProjection * Vec4(world, 1.0f);
        return Vec3(clip) / clip.w;
    };

    // The origin is straight ahead: center of the screen, inside [0, 1] depth.
    const Vec3 center = project(Vec3(0.0f));
    EXPECT_NEAR(center.x, 0.0f, 1e-5f);
    EXPECT_NEAR(center.y, 0.0f, 1e-5f);
    EXPECT_GT(center.z, 0.0f);
    EXPECT_LT(center.z, 1.0f);

    // Clip space has Y pointing down: a point above the origin lands at y < 0,
    // a point to the right at x > 0.
    EXPECT_LT(project(Vec3(0.0f, 1.0f, 0.0f)).y, 0.0f);
    EXPECT_GT(project(Vec3(1.0f, 0.0f, 0.0f)).x, 0.0f);
}
