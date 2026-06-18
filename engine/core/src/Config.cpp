#include <bdex/core/Config.hpp>

#include <bdex/core/FileSystem.hpp>

#include <charconv>

namespace bdex {
namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const usize begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    const usize end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

}  // namespace

Result<Config> Config::parse(std::string_view text) {
    Config config;
    std::string section;
    usize lineNumber = 0;

    while (!text.empty()) {
        ++lineNumber;
        const usize newline = text.find('\n');
        std::string_view line = trim(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                return fail("config line {}: missing ']' in section header", lineNumber);
            }
            section = std::string(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const usize equals = line.find('=');
        if (equals == std::string_view::npos) {
            return fail("config line {}: expected 'key = value'", lineNumber);
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (key.empty()) {
            return fail("config line {}: empty key", lineNumber);
        }
        config.set(section.empty() ? std::string(key) : section + "." + std::string(key), value);
    }
    return config;
}

Result<Config> Config::loadFile(const std::filesystem::path& path) {
    auto text = fs::readTextFile(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parse(*text);
}

void Config::set(std::string_view key, std::string_view value) {
    values_.insert_or_assign(std::string(key), std::string(value));
}

void Config::setDefault(std::string_view key, std::string_view value) {
    values_.try_emplace(std::string(key), std::string(value));
}

void Config::merge(const Config& other) {
    for (const auto& [key, value] : other.values_) {
        values_.insert_or_assign(key, value);
    }
}

bool Config::has(std::string_view key) const {
    return values_.contains(key);
}

std::optional<std::string> Config::find(std::string_view key) const {
    const auto it = values_.find(key);
    return it == values_.end() ? std::nullopt : std::optional(it->second);
}

std::string Config::getString(std::string_view key, std::string_view fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? std::string(fallback) : it->second;
}

i64 Config::getInt(std::string_view key, i64 fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    i64 value = 0;
    const char* end = it->second.data() + it->second.size();
    const auto [ptr, ec] = std::from_chars(it->second.data(), end, value);
    return ec == std::errc{} && ptr == end ? value : fallback;
}

f64 Config::getFloat(std::string_view key, f64 fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    f64 value = 0.0;
    const char* end = it->second.data() + it->second.size();
    const auto [ptr, ec] = std::from_chars(it->second.data(), end, value);
    return ec == std::errc{} && ptr == end ? value : fallback;
}

bool Config::getBool(std::string_view key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    const std::string& value = it->second;
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

std::vector<std::string> Config::getList(std::string_view key) const {
    std::vector<std::string> result;
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return result;
    }
    std::string_view rest = it->second;
    while (!rest.empty()) {
        const usize comma = rest.find(',');
        const std::string_view item = trim(rest.substr(0, comma));
        if (!item.empty()) {
            result.emplace_back(item);
        }
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    }
    return result;
}

}  // namespace bdex
