#pragma once

// Shared constants and small helpers for Hyrule Hide & Seek.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include <mods/svc/log.hpp>

namespace hs {

// Player ids come from the relay: 1..16. Index 0 is unused so arrays can be indexed by id.
constexpr int kMaxPlayers = 16;
constexpr int kSlots = kMaxPlayers + 1;

#ifdef HS_TEST_CLOCK
uint64_t test_now_ms();  // tests/ drive time by hand
inline uint64_t now_ms() {
    return test_now_ms();
}
#else
inline uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
#endif

// Tunic colours. Index 0 keeps Link's own green. Every player gets a different one.
struct TunicColor {
    const char* name;
    uint8_t r, g, b;  // name tag / UI colour
    float hue;        // tunic hue in degrees, < 0 keeps the original hue
    float sat;        // saturation multiplier
    float val;        // brightness multiplier
};

constexpr TunicColor kColors[kMaxPlayers] = {
    {"Green", 90, 200, 90, -1.0f, 1.0f, 1.0f},
    {"Red", 235, 70, 60, 0.0f, 1.25f, 1.05f},
    {"Blue", 70, 120, 245, 222.0f, 1.25f, 1.05f},
    {"Yellow", 245, 215, 60, 52.0f, 1.3f, 1.35f},
    {"Purple", 160, 90, 225, 275.0f, 1.2f, 1.0f},
    {"Orange", 250, 150, 50, 28.0f, 1.35f, 1.25f},
    {"Cyan", 70, 215, 230, 188.0f, 1.2f, 1.2f},
    {"Pink", 250, 130, 190, 330.0f, 0.9f, 1.4f},
    {"White", 240, 240, 240, 0.0f, 0.05f, 1.9f},
    {"Black", 90, 90, 100, 230.0f, 0.15f, 0.6f},
    {"Lime", 170, 240, 60, 88.0f, 1.3f, 1.3f},
    {"Teal", 40, 170, 150, 170.0f, 1.1f, 0.85f},
    {"Brown", 170, 110, 60, 25.0f, 0.9f, 0.85f},
    {"Navy", 50, 70, 170, 230.0f, 1.2f, 0.75f},
    {"Magenta", 230, 60, 200, 305.0f, 1.3f, 1.1f},
    {"Gold", 225, 180, 70, 44.0f, 1.0f, 1.1f},
};

inline const TunicColor& color_of(int index) {
    return kColors[(index >= 0 && index < kMaxPlayers) ? index : 0];
}

// Copies a C string into a fixed buffer, always terminated.
template <size_t N>
void copy_str(char (&dst)[N], const char* src) {
    std::strncpy(dst, src != nullptr ? src : "", N - 1);
    dst[N - 1] = '\0';
}

}  // namespace hs
