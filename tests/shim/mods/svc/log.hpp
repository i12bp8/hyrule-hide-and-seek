#pragma once
// Test stand-in for the SDK's formatted logging (std::format instead of fmt).
#include <cstdio>
#include <cstdlib>
#include <format>
#include <utility>

namespace mods::log {
template <typename... A>
void write(const char* level, std::format_string<A...> f, A&&... a) {
    if (std::getenv("HS_TEST_LOG") != nullptr) {
        std::printf("  [%s] %s\n", level, std::format(f, std::forward<A>(a)...).c_str());
    }
}
template <typename... A>
void info(std::format_string<A...> f, A&&... a) { write("info", f, std::forward<A>(a)...); }
template <typename... A>
void warn(std::format_string<A...> f, A&&... a) { write("warn", f, std::forward<A>(a)...); }
template <typename... A>
void error(std::format_string<A...> f, A&&... a) { write("error", f, std::forward<A>(a)...); }
}  // namespace mods::log
