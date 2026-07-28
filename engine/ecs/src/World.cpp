#include <bdex/ecs/World.hpp>

namespace bdex::ecs {

Entity World::create() {
    u32 index;
    if (!freeSlots_.empty()) {
        index = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        index = static_cast<u32>(generations_.size());
        generations_.push_back(1);
        aliveFlags_.push_back(false);
    }
    aliveFlags_[index] = true;
    ++aliveCount_;
    return Entity{index, generations_[index]};
}

void World::destroy(Entity entity) {
    if (!alive(entity)) {
        return;
    }
    for (auto& [type, pool] : pools_) {
        pool->remove(entity.index);
    }
    aliveFlags_[entity.index] = false;
    ++generations_[entity.index];  // invalidates outstanding handles
    freeSlots_.push_back(entity.index);
    --aliveCount_;
}

bool World::alive(Entity entity) const {
    return entity.index < generations_.size() && aliveFlags_[entity.index] &&
           generations_[entity.index] == entity.generation;
}

void World::clear() {
    for (auto& [type, pool] : pools_) {
        pool->clear();
    }
    freeSlots_.clear();
    for (u32 index = static_cast<u32>(generations_.size()); index-- > 0;) {
        if (aliveFlags_[index]) {
            aliveFlags_[index] = false;
            ++generations_[index];
        }
        freeSlots_.push_back(index);
    }
    aliveCount_ = 0;
}

}  // namespace bdex::ecs
