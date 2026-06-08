#pragma once

namespace bdex::detail {

[[noreturn]] void assertFail(const char* condition, const char* file, int line,
                             const char* message);

}  // namespace bdex::detail

/// Checks an invariant. Failing an assert is a programming error: the engine
/// logs the location and aborts. Enabled in every build type unless
/// BDEX_DISABLE_ASSERTS is defined.
///
///   BDEX_ASSERT(index < size);
///   BDEX_ASSERT(ptr != nullptr, "device must be created first");
#if defined(BDEX_DISABLE_ASSERTS)
#define BDEX_ASSERT(condition, ...) ((void)sizeof(condition))
#else
#define BDEX_ASSERT(condition, ...)                                                     \
    do {                                                                                \
        if (!(condition)) [[unlikely]] {                                                \
            ::bdex::detail::assertFail(#condition, __FILE__, __LINE__, "" __VA_ARGS__); \
        }                                                                               \
    } while (false)
#endif
