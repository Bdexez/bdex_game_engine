#include <bdex/core/Assert.hpp>

#include <bdex/core/Log.hpp>

#include <cstdio>
#include <cstdlib>

namespace bdex::detail {

void assertFail(const char* condition, const char* file, int line, const char* message) {
    // Write straight to stderr as well: the logger may be filtered or redirected.
    std::fprintf(stderr, "ASSERTION FAILED: %s\n  at %s:%d\n  %s\n", condition, file, line,
                 message);
    std::fflush(stderr);
    log::error("assert", "{} ({}:{}) {}", condition, file, line, message);
    std::abort();
}

}  // namespace bdex::detail
