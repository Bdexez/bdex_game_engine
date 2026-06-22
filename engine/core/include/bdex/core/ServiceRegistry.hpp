#pragma once

#include <bdex/core/Assert.hpp>
#include <bdex/core/TypeId.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bdex {

/// Registry of engine-wide services, keyed by *interface* type.
///
/// This is the seam that keeps modules independent from each other: a module
/// provides an implementation of an interface (`rhi::IDevice`,
/// `platform::IWindowSystem`...), consumers ask for the interface and never
/// see the concrete type nor the module it comes from.
///
///   services.provide<rhi::IDevice>(std::move(vulkanDevice));
///   rhi::IDevice& device = services.get<rhi::IDevice>();
class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ~ServiceRegistry() { clear(); }
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    /// Registers an owned service. Replaces any previous provider.
    template <class Interface>
    void provide(std::shared_ptr<Interface> service) {
        BDEX_ASSERT(service != nullptr, "cannot provide a null service");
        insert(typeId<Interface>(), typeName<Interface>(), std::move(service));
    }

    /// Registers a service owned elsewhere; the caller guarantees it outlives
    /// its registration (call remove<Interface>() before destroying it).
    template <class Interface>
    void provideRef(Interface& service) {
        insert(typeId<Interface>(), typeName<Interface>(),
               std::shared_ptr<Interface>(std::shared_ptr<void>{}, &service));
    }

    template <class Interface>
    [[nodiscard]] Interface* tryGet() const {
        const auto it = index_.find(typeId<Interface>());
        return it == index_.end() ? nullptr
                                  : static_cast<Interface*>(entries_[it->second].service.get());
    }

    /// Returns the service, aborting with a clear message if nobody provides it.
    template <class Interface>
    [[nodiscard]] Interface& get() const {
        Interface* service = tryGet<Interface>();
        if (service == nullptr) {
            missing(typeName<Interface>());
        }
        return *service;
    }

    template <class Interface>
    [[nodiscard]] bool has() const {
        return index_.contains(typeId<Interface>());
    }

    template <class Interface>
    void remove() {
        erase(typeId<Interface>());
    }

    /// Names of the registered interfaces, in registration order.
    [[nodiscard]] std::vector<std::string> names() const;

    /// Destroys services in reverse registration order.
    void clear();

private:
    struct Entry {
        TypeId type = 0;
        std::string name;
        std::shared_ptr<void> service;
    };

    void insert(TypeId type, std::string_view name, std::shared_ptr<void> service) {
        erase(type);
        index_[type] = entries_.size();
        entries_.push_back({type, std::string(name), std::move(service)});
    }

    void erase(TypeId type) {
        const auto it = index_.find(type);
        if (it == index_.end()) {
            return;
        }
        const usize position = it->second;
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(position));
        index_.erase(it);
        for (auto& [key, value] : index_) {
            if (value > position) {
                --value;
            }
        }
    }

    [[noreturn]] static void missing(std::string_view name) {
        const std::string message = "no provider for service '" + std::string(name) + "'";
        detail::assertFail("service != nullptr", __FILE__, __LINE__, message.c_str());
    }

    std::vector<Entry> entries_;
    std::unordered_map<TypeId, usize> index_;
};

inline std::vector<std::string> ServiceRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        result.push_back(entry.name);
    }
    return result;
}

inline void ServiceRegistry::clear() {
    while (!entries_.empty()) {
        entries_.pop_back();
    }
    index_.clear();
}

}  // namespace bdex
