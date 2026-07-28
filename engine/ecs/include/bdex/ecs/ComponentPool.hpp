#pragma once

#include <bdex/core/Assert.hpp>
#include <bdex/core/Types.hpp>

#include <span>
#include <utility>
#include <vector>

namespace bdex::ecs {

/// Type-erased view of a component pool, used by World to destroy entities
/// without knowing the component types.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    virtual void remove(u32 entityIndex) = 0;
    [[nodiscard]] virtual bool contains(u32 entityIndex) const = 0;
    [[nodiscard]] virtual usize size() const = 0;
    /// Indices of the entities owning a component, in dense storage order.
    [[nodiscard]] virtual std::span<const u32> entities() const = 0;
    virtual void clear() = 0;
};

/// Sparse-set storage: components are packed in a contiguous array (fast
/// iteration), with O(1) lookup, insertion and removal by entity index.
template <class T>
class ComponentPool final : public IComponentPool {
public:
    template <class... Args>
    T& emplace(u32 entityIndex, Args&&... args) {
        if (contains(entityIndex)) {
            T& existing = dense_[sparse_[entityIndex]];
            existing = T{std::forward<Args>(args)...};
            return existing;
        }
        if (entityIndex >= sparse_.size()) {
            sparse_.resize(static_cast<usize>(entityIndex) + 1, kAbsent);
        }
        sparse_[entityIndex] = static_cast<u32>(dense_.size());
        denseEntities_.push_back(entityIndex);
        dense_.push_back(T{std::forward<Args>(args)...});
        return dense_.back();
    }

    void remove(u32 entityIndex) override {
        if (!contains(entityIndex)) {
            return;
        }
        // Swap-and-pop keeps the dense arrays contiguous.
        const u32 slot = sparse_[entityIndex];
        const u32 last = static_cast<u32>(dense_.size() - 1);
        if (slot != last) {
            dense_[slot] = std::move(dense_[last]);
            denseEntities_[slot] = denseEntities_[last];
            sparse_[denseEntities_[slot]] = slot;
        }
        dense_.pop_back();
        denseEntities_.pop_back();
        sparse_[entityIndex] = kAbsent;
    }

    [[nodiscard]] bool contains(u32 entityIndex) const override {
        return entityIndex < sparse_.size() && sparse_[entityIndex] != kAbsent;
    }

    [[nodiscard]] T& get(u32 entityIndex) {
        BDEX_ASSERT(contains(entityIndex), "entity does not have this component");
        return dense_[sparse_[entityIndex]];
    }

    [[nodiscard]] T* tryGet(u32 entityIndex) {
        return contains(entityIndex) ? &dense_[sparse_[entityIndex]] : nullptr;
    }

    [[nodiscard]] usize size() const override { return dense_.size(); }
    [[nodiscard]] std::span<const u32> entities() const override { return denseEntities_; }

    void clear() override {
        sparse_.clear();
        dense_.clear();
        denseEntities_.clear();
    }

private:
    static constexpr u32 kAbsent = ~0u;

    std::vector<u32> sparse_;         // entity index -> dense slot
    std::vector<u32> denseEntities_;  // dense slot -> entity index
    std::vector<T> dense_;
};

}  // namespace bdex::ecs
