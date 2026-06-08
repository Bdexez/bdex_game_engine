#pragma once

#include <expected>
#include <format>
#include <string>
#include <utility>

namespace bdex {

/// Error carried by a failed Result. Errors are for recoverable conditions
/// (missing file, unsupported device...); programming errors use BDEX_ASSERT.
struct Error {
    std::string message;
};

template <class T = void>
using Result = std::expected<T, Error>;

/// Builds a failed Result: `return fail("cannot open {}", path);`
template <class... Args>
[[nodiscard]] std::unexpected<Error> fail(std::format_string<Args...> fmt, Args&&... args) {
    return std::unexpected(Error{std::format(fmt, std::forward<Args>(args)...)});
}

}  // namespace bdex
