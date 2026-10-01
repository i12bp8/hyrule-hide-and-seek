#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace hs {
constexpr uint8_t kArenaHeartPieces = 25;  // five hearts, save capacity uses fifths
constexpr uint16_t kArenaLife = 20;        // current life uses quarters
constexpr uint64_t kTrackingCooldownMs = 25000;
constexpr uint64_t kTrackingRevealMs = 3000;
constexpr uint64_t kTauntRevealMs = 3000;
constexpr uint64_t kTauntCooldownMs = 4000;
constexpr uint64_t kSwingWindowMs = 650;
constexpr uint16_t life_after_miss(uint16_t life, uint8_t quarters = 2) {
    return static_cast<uint16_t>(life - std::min<uint16_t>(quarters, life));
}

// The text keeps a broad sector/range; the arrow carries the exact horizontal bearing.
// Screen-space up means ahead and down means behind the hunter's current view.
struct SearchClue {
    const char* direction = "Ahead";
    const char* range = "Near";
    float arrowX = 0;
    float arrowY = -1;
};
inline SearchClue search_clue(float right, float forward, float distance) {
    const bool sideways = std::fabs(right) > std::fabs(forward);
    const float length = std::hypot(right, forward);
    return {sideways ? (right > 0 ? "Right" : "Left") :
            (forward >= 0 ? "Ahead" : "Behind"),
            distance < 1200 ? "Near" : distance < 3500 ? "In the area" : "Distant",
            length > 0.001f ? right / length : 0.0f,
            length > 0.001f ? -forward / length : -1.0f};
}
inline SearchClue search_clue_from_view(float deltaX, float deltaZ,
    float viewForwardX, float viewForwardZ, float distance) {
    const float length = std::hypot(viewForwardX, viewForwardZ);
    if (length > 0.001f) {
        viewForwardX /= length;
        viewForwardZ /= length;
    } else {
        viewForwardX = 0.0f;
        viewForwardZ = -1.0f;
    }
    return search_clue(-viewForwardZ * deltaX + viewForwardX * deltaZ,
        viewForwardX * deltaX + viewForwardZ * deltaZ, distance);
}
constexpr uint32_t final_clue_window_ms(uint16_t seconds, uint16_t seekSeconds) {
    // A short custom hunt still gets a quiet first half.
    return std::min<uint32_t>(seconds * 1000u, seekSeconds * 500u);
}
}
