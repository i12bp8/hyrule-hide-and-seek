#pragma once

#include <algorithm>
#include <cstdint>

namespace hs::scoring {
// Both starting roles can earn the same round maximum: 12 objective + 8 bonus + 6 win.
constexpr int kObjectivePoints = 12;
constexpr int kBonusPoints = 8;
constexpr int kWinPoints = 6;
constexpr int kPersonalFindPoints = 3;

// Style bonuses. They share the bonus allowance, so no single trick can be farmed.
constexpr int kTauntPoints = 1;
constexpr int kBoldTauntPoints = 2;       // a hunter within kBoldTauntRange
constexpr float kBoldTauntRange = 1500.0f;
constexpr int kDecoyFoolPoints = 1;       // per hunter fooled, at most kMaxDecoyFools a round
constexpr int kMaxDecoyFools = 3;
constexpr int kCloseCallPoints = 1;       // a hunter's miss within kCloseCallRange of you
constexpr int kMaxCloseCalls = 2;
constexpr float kCloseCallRange = 400.0f;
constexpr uint64_t kCloseCallGapMs = 8000;
constexpr int kLastStandingPoints = 2;    // the only prop left when time runs out
constexpr int kFirstBloodPoints = 1;
constexpr int kQuickFindPoints = 1;       // another find within kQuickFindMs
constexpr uint64_t kQuickFindMs = 20000;

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
