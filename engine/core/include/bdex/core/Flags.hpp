#pragma once

#include <type_traits>

/// Enables bitwise operators on a scoped enum used as a set of flags:
///
///   enum class Usage : u32 { None = 0, Read = 1, Write = 2 };
///   BDEX_ENUM_FLAGS(Usage)
///
///   Usage usage = Usage::Read | Usage::Write;
///   if (hasFlag(usage, Usage::Write)) { ... }
#define BDEX_ENUM_FLAGS(Enum)                                                        \
    [[nodiscard]] constexpr Enum operator|(Enum a, Enum b) {                         \
        using U = std::underlying_type_t<Enum>;                                      \
        return static_cast<Enum>(static_cast<U>(a) | static_cast<U>(b));             \
    }                                                                                \
    [[nodiscard]] constexpr Enum operator&(Enum a, Enum b) {                         \
        using U = std::underlying_type_t<Enum>;                                      \
        return static_cast<Enum>(static_cast<U>(a) & static_cast<U>(b));             \
    }                                                                                \
    constexpr Enum& operator|=(Enum& a, Enum b) {                                    \
        a = a | b;                                                                   \
        return a;                                                                    \
    }                                                                                \
    [[nodiscard]] constexpr bool hasFlag(Enum value, Enum flag) {                    \
        using U = std::underlying_type_t<Enum>;                                      \
        return (static_cast<U>(value) & static_cast<U>(flag)) == static_cast<U>(flag) && \
               static_cast<U>(flag) != 0;                                            \
    }
