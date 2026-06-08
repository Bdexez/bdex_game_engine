#include <bdex/core/Log.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define BDEX_ISATTY(fd) _isatty(fd)
#define BDEX_STDERR_FD 2
#else
#include <unistd.h>
#define BDEX_ISATTY(fd) isatty(fd)
#define BDEX_STDERR_FD STDERR_FILENO
#endif

namespace bdex::log {
namespace {

struct State {
    std::mutex mutex;
    std::atomic<Level> level{Level::Info};
    std::vector<std::pair<u32, Sink>> sinks;
    u32 nextSinkId = 1;
    bool consoleEnabled = true;
    bool consoleColors = BDEX_ISATTY(BDEX_STDERR_FD) != 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

// Function-local static: safe to use from static initializers of any module.
State& state() {
    static State instance;
    return instance;
}

const char* levelColor(Level level) {
    switch (level) {
        case Level::Trace: return "\x1b[90m";
        case Level::Debug: return "\x1b[36m";
        case Level::Info: return "\x1b[32m";
        case Level::Warn: return "\x1b[33m";
        case Level::Error: return "\x1b[31m";
        case Level::Off: break;
    }
    return "";
}

void writeConsole(State& s, const Record& record) {
    const f64 seconds =
        std::chrono::duration<f64>(std::chrono::steady_clock::now() - s.start).count();
    const std::string line = s.consoleColors
        ? std::format("\x1b[90m[{:9.3f}]\x1b[0m {}{:<5}\x1b[0m \x1b[1m{}\x1b[0m: {}\n", seconds,
                      levelColor(record.level), levelName(record.level), record.category,
                      record.message)
        : std::format("[{:9.3f}] {:<5} {}: {}\n", seconds, levelName(record.level),
                      record.category, record.message);
    std::fwrite(line.data(), 1, line.size(), stderr);
}

}  // namespace

void setLevel(Level level) {
    state().level.store(level, std::memory_order_relaxed);
}

Level level() {
    return state().level.load(std::memory_order_relaxed);
}

bool enabled(Level level) {
    return level != Level::Off && level >= log::level();
}

std::string_view levelName(Level level) {
    switch (level) {
        case Level::Trace: return "trace";
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warn: return "warn";
        case Level::Error: return "error";
        case Level::Off: return "off";
    }
    return "?";
}

bool parseLevel(std::string_view text, Level& out) {
    for (Level candidate : {Level::Trace, Level::Debug, Level::Info, Level::Warn, Level::Error,
                            Level::Off}) {
        if (text == levelName(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

u32 addSink(Sink sink) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    const u32 id = s.nextSinkId++;
    s.sinks.emplace_back(id, std::move(sink));
    return id;
}

void removeSink(u32 id) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    std::erase_if(s.sinks, [id](const auto& entry) { return entry.first == id; });
}

void setConsoleEnabled(bool enabled) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.consoleEnabled = enabled;
}

void write(Level level, std::string_view category, std::string_view message) {
    if (!enabled(level)) {
        return;
    }
    State& s = state();
    const Record record{level, category, message};
    std::lock_guard lock(s.mutex);
    if (s.consoleEnabled) {
        writeConsole(s, record);
    }
    for (const auto& [id, sink] : s.sinks) {
        sink(record);
    }
}

}  // namespace bdex::log
