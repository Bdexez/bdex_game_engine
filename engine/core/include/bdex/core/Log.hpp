#pragma once

#include <bdex/core/Types.hpp>

#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace bdex::log {

enum class Level : u8 { Trace, Debug, Info, Warn, Error, Off };

struct Record {
    Level level;
    std::string_view category;
    std::string_view message;
};

using Sink = std::function<void(const Record&)>;

void setLevel(Level level);
[[nodiscard]] Level level();
[[nodiscard]] bool enabled(Level level);
[[nodiscard]] std::string_view levelName(Level level);
/// Parses "trace", "debug", "info", "warn", "error", "off" (case-sensitive).
[[nodiscard]] bool parseLevel(std::string_view text, Level& out);

/// Adds an extra sink (file, in-game console, test capture...). Thread-safe.
u32 addSink(Sink sink);
void removeSink(u32 id);
/// The built-in stderr sink is enabled by default.
void setConsoleEnabled(bool enabled);

void write(Level level, std::string_view category, std::string_view message);

template <class... Args>
void log(Level level, std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(level)) {
        write(level, category, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <class... Args>
void trace(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Trace, category, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void debug(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Debug, category, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void info(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Info, category, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void warn(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Warn, category, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void error(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Error, category, fmt, std::forward<Args>(args)...);
}

}  // namespace bdex::log
