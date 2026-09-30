#pragma once

#include <algorithm>
#include <cstdint>

namespace hs::scoring {
constexpr int kObjectivePoints = 12;
constexpr int kBonusPoints = 6;
constexpr int kWinPoints = 6;
constexpr int kPersonalFindPoints = 3;

inline int personal_find(int startingHiders) {
    return startingHiders == 1 ? kBonusPoints : kPersonalFindPoints;
}

inline int survival(uint64_t elapsedMs, uint16_t seekSecs) {
    const uint64_t duration = std::max<uint64_t>(1, seekSecs) * 1000;
    return static_cast<int>(std::min(elapsedMs, duration) * kObjectivePoints / duration);
}

inline int captures(int found, int startingHiders) {
    return std::clamp(found, 0, startingHiders) * kObjectivePoints / std::max(1, startingHiders);
}

inline int bonus(int alreadyEarned, int requested) {
    return std::clamp(requested, 0, std::max(0, kBonusPoints - alreadyEarned));
}
}  // namespace hs::scoring
