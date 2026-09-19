#include <bdex/scene/Scene.hpp>

#include <algorithm>

namespace bdex::scene {
namespace {

// Guards against cyclic Parent chains.
constexpr u32 kMaxHierarchyDepth = 64;

Mat4 computeWorldMatrix(ecs::World& world, ecs::Entity entity, u32 depth) {
    const Mat4 local = world.get<Transform>(entity).localMatrix();
    if (depth >= kMaxHierarchyDepth) {
        return local;
    }
    const Parent* parent = world.tryGet<Parent>(entity);
    if (parent == nullptr || !world.has<Transform>(parent->entity)) {
        return local;
    }
    return computeWorldMatrix(world, parent->entity, depth + 1) * local;
}

}  // namespace

ecs::Entity Scene::createEntity(std::string_view name) {
    const ecs::Entity entity = world_.create();
    world_.emplace<Name>(entity, std::string(name));
    world_.emplace<Transform>(entity);
    world_.emplace<WorldTransform>(entity);
    return entity;
}

Scene& SceneManager::create(std::string name) {
    scenes_.push_back(std::make_unique<Scene>(std::move(name)));
    if (active_ == nullptr) {
        active_ = scenes_.back().get();
    }
    return *scenes_.back();
}

void SceneManager::destroy(Scene& scene) {
    if (active_ == &scene) {
        active_ = nullptr;
    }
    std::erase_if(scenes_, [&scene](const auto& owned) { return owned.get() == &scene; });
}

void updateWorldTransforms(ecs::World& world) {
    // Each entity walks up its parent chain: O(n * depth). Hierarchies are
    // shallow in practice; a dirty-flag pass can replace this if it ever
    // shows up in a profile.
    world.view<Transform, WorldTransform>().each(
        [&world](ecs::Entity entity, Transform&, WorldTransform& worldTransform) {
            worldTransform.matrix = computeWorldMatrix(world, entity, 0);
        });
}

}  // namespace bdex::scene
