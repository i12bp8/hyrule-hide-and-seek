#pragma once
#include <cstdint>

namespace hs {
constexpr uint8_t kArenaHeartPieces = 25;  // five hearts, save capacity uses fifths
constexpr uint16_t kArenaLife = 20;        // current life uses quarters
constexpr uint64_t kTrackingCooldownMs = 25000;
constexpr uint64_t kTrackingRevealMs = 3000;
constexpr uint64_t kTauntRevealMs = 3000;
constexpr uint64_t kTauntCooldownMs = 4000;
constexpr uint16_t life_after_miss(uint16_t life) { return life > 1 ? life - 1 : life; }
}
