#pragma once

#include <bdex/core/Assert.hpp>
#include <bdex/core/TypeId.hpp>
#include <bdex/ecs/ComponentPool.hpp>
#include <bdex/ecs/Entity.hpp>

#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bdex::ecs {

template <class... Components>
class View;

/// Container of entities and their components.
///
/// Component types need no registration: any movable type can be attached.
/// Pools are keyed by bdex::typeId, so a component type defined in a plugin
/// and the same type used by the executable share one pool.
///
/// Systems are plain functions over views, scheduled through the engine
/// Scheduler:
///
///   world.view<Transform, Velocity>().each([&](Transform& t, Velocity& v) {
///       t.position += v.value * dt;
///   });
class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    [[nodiscard]] Entity create();
    /// Destroys the entity and all its components. Stale handles are ignored.
    void destroy(Entity entity);
    [[nodiscard]] bool alive(Entity entity) const;
    [[nodiscard]] usize entityCount() const { return aliveCount_; }

    /// Destroys every entity and component.
    void clear();

    /// Attaches (or replaces) a component and returns it.
    template <class T, class... Args>
    T& emplace(Entity entity, Args&&... args) {
        BDEX_ASSERT(alive(entity), "emplace on a dead entity");
        return pool<T>().emplace(entity.index, std::forward<Args>(args)...);
    }

    template <class T>
    void remove(Entity entity) {
        if (alive(entity)) {
            pool<T>().remove(entity.index);
        }
    }

    template <class T>
    [[nodiscard]] T& get(Entity entity) {
        BDEX_ASSERT(alive(entity), "get on a dead entity");
        return pool<T>().get(entity.index);
    }

    template <class T>
    [[nodiscard]] T* tryGet(Entity entity) {
        return alive(entity) ? pool<T>().tryGet(entity.index) : nullptr;
    }

    template <class... Ts>
    [[nodiscard]] bool has(Entity entity) {
        return alive(entity) && (pool<Ts>().contains(entity.index) && ...);
    }

    /// Iterates entities owning *all* the listed components (types must be
    /// distinct).
    template <class... Components>
    [[nodiscard]] View<Components...> view() {
        return View<Components...>(*this, pool<Components>()...);
    }

    /// Rebuilds the full handle of a live entity from its index.
    [[nodiscard]] Entity entityAt(u32 index) const {
        BDEX_ASSERT(index < generations_.size());
        return Entity{index, generations_[index]};
    }

private:
    template <class T>
    ComponentPool<T>& pool() {
        using Stored = std::remove_cvref_t<T>;
        const TypeId id = typeId<Stored>();
        auto it = pools_.find(id);
        if (it == pools_.end()) {
            it = pools_.emplace(id, std::make_unique<ComponentPool<Stored>>()).first;
        }
        return static_cast<ComponentPool<Stored>&>(*it->second);
    }

    std::vector<u32> generations_;  // per slot; odd/even is irrelevant, only equality matters
    std::vector<bool> aliveFlags_;
    std::vector<u32> freeSlots_;
    usize aliveCount_ = 0;
    std::unordered_map<TypeId, std::unique_ptr<IComponentPool>> pools_;
};

/// Result of World::view<...>(). Cheap to create; holds direct references to
/// the pools so iteration performs no map lookup.
template <class... Components>
class View {
public:
    View(World& world, ComponentPool<Components>&... pools)
        : world_(&world), pools_(&pools...) {}

    /// Calls `fn` for each matching entity, as either
    /// `fn(Entity, Components&...)` or `fn(Components&...)`.
    ///
    /// Iteration walks the smallest pool backwards, so the callback may
    /// destroy the current entity or remove its components. Adding entities
    /// or components during iteration is allowed; whether they are visited
    /// in the same pass is unspecified.
    template <class Fn>
    void each(Fn&& fn) {
        IComponentPool* driver = smallestPool();
        for (usize i = driver->size(); i-- > 0;) {
            // Re-read the span every iteration: the callback may have grown
            // or shrunk the pool.
            const std::span<const u32> entities = driver->entities();
            if (i >= entities.size()) {
                continue;
            }
            const u32 index = entities[i];
            if (!(std::get<ComponentPool<Components>*>(pools_)->contains(index) && ...)) {
                continue;
            }
            if constexpr (std::is_invocable_v<Fn, Entity, Components&...>) {
                fn(world_->entityAt(index),
                   std::get<ComponentPool<Components>*>(pools_)->get(index)...);
            } else {
                fn(std::get<ComponentPool<Components>*>(pools_)->get(index)...);
            }
        }
    }

    /// Number of matching entities (walks the smallest pool).
    [[nodiscard]] usize count() {
        usize result = 0;
        IComponentPool* driver = smallestPool();
        for (const u32 index : driver->entities()) {
            if ((std::get<ComponentPool<Components>*>(pools_)->contains(index) && ...)) {
                ++result;
            }
        }
        return result;
    }

private:
    IComponentPool* smallestPool() {
        IComponentPool* smallest = nullptr;
        const auto consider = [&smallest](IComponentPool* candidate) {
            if (smallest == nullptr || candidate->size() < smallest->size()) {
                smallest = candidate;
            }
        };
        (consider(std::get<ComponentPool<Components>*>(pools_)), ...);
        return smallest;
    }

    World* world_;
    std::tuple<ComponentPool<Components>*...> pools_;
};

}  // namespace bdex::ecs
