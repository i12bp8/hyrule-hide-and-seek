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
constexpr uint16_t life_after_miss(uint16_t life, uint8_t quarters = 2) {
    return life > 1 ? static_cast<uint16_t>(life - std::min<uint16_t>(quarters, life - 1)) : life;
}
constexpr bool exhausted_after_miss(uint16_t life, uint8_t quarters) {
    return quarters != 0 && life_after_miss(life, quarters) == 1;
}

// Capture a broad sector and range once. Keeping only these categories prevents camera turns
// or movement during a clue from narrowing it into an exact bearing or a world marker.
struct SearchClue {
    const char* direction = "Ahead";
    const char* range = "Near";
    int8_t arrowX = 0;
    int8_t arrowY = -1; // screen-space up means ahead, down means behind
};
inline SearchClue search_clue(float right, float forward, float distance) {
    const bool sideways = std::fabs(right) > std::fabs(forward);
    return {sideways ? (right > 0 ? "Right" : "Left") :
            (forward >= 0 ? "Ahead" : "Behind"),
            distance < 1200 ? "Near" : distance < 3500 ? "In the area" : "Distant",
            static_cast<int8_t>(sideways ? (right > 0 ? 1 : -1) : 0),
            static_cast<int8_t>(sideways ? 0 : (forward >= 0 ? -1 : 1))};
}
constexpr uint32_t final_clue_window_ms(uint16_t seconds, uint16_t seekSeconds) {
    // A short custom hunt still gets a quiet first half.
    return std::min<uint32_t>(seconds * 1000u, seekSeconds * 500u);
}
}
