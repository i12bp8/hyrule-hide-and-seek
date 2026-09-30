#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <cmath>

namespace hs {
// One packet of delay absorbs 10 Hz arrivals. Stop prediction after 100 ms of jitter.
inline PlayerState interpolate_state(const PlayerState& before, uint64_t beforeAt,
    const PlayerState& latest, uint64_t latestAt, uint64_t now) {
    PlayerState out = latest;
    if (beforeAt == 0 || latestAt <= beforeAt || std::strcmp(before.stage, latest.stage) != 0) return out;
    const float dx = latest.x - before.x, dy = latest.y - before.y, dz = latest.z - before.z;
    if (dx * dx + dy * dy + dz * dz > 600.0f * 600.0f) return out;
    const uint64_t renderAt = now > 100 ? now - 100 : 0;
    const uint64_t cappedAt = std::min(renderAt, latestAt + 100);
    const float t = std::clamp(static_cast<float>(static_cast<int64_t>(cappedAt) - static_cast<int64_t>(beforeAt)) /
        static_cast<float>(latestAt - beforeAt), 0.0f, 2.0f);
    out.x = before.x + dx * t; out.y = before.y + dy * t; out.z = before.z + dz * t;
    const auto angle = [t](int16_t a, int16_t b) {
        const int16_t delta = static_cast<int16_t>(static_cast<uint16_t>(b) - static_cast<uint16_t>(a));
        return static_cast<int16_t>(a + static_cast<int32_t>(delta * std::min(t, 1.0f)));
    };
    out.yaw = angle(before.yaw, latest.yaw); out.propYaw = angle(before.propYaw, latest.propYaw);
    return out;
}
}
