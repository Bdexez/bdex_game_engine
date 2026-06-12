#pragma once

#include <bdex/core/Types.hpp>

#include <string_view>

namespace bdex {

/// 64-bit FNV-1a, usable at compile time.
[[nodiscard]] constexpr u64 fnv1a(std::string_view text) {
    u64 hash = 0xcbf29ce484222325ull;
    for (const char c : text) {
        hash ^= static_cast<u8>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

/// Fully qualified name of T, extracted from the compiler's function signature.
template <class T>
[[nodiscard]] constexpr std::string_view typeName() {
#if defined(__clang__) || defined(__GNUC__)
    const std::string_view signature = __PRETTY_FUNCTION__;
    const usize begin = signature.find("T = ") + 4;
    const usize end = signature.find_first_of(";]", begin);
    return signature.substr(begin, end - begin);
#elif defined(_MSC_VER)
    const std::string_view signature = __FUNCSIG__;
    const usize begin = signature.find("typeName<") + 9;
    const usize end = signature.rfind(">(void)");
    return signature.substr(begin, end - begin);
#else
#error "bdex::typeName: unsupported compiler"
#endif
}

/// Stable identifier of a type.
///
/// It is derived from the type *name*, not from the address of a static or a
/// runtime counter, so the same type yields the same id in the executable and
/// in every plugin. This is what lets services, events and ECS components
/// cross shared-library boundaries safely.
using TypeId = u64;

template <class T>
[[nodiscard]] constexpr TypeId typeId() {
    return fnv1a(typeName<T>());
}

}  // namespace bdex
