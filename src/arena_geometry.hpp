#pragma once

#include <algorithm>
#include <cmath>

namespace hs::arena {

struct Point { float x, y, z; };
struct ExitVolume {
    Point center;
    float halfX, height, halfZ;
    float sine, cosine;
    bool circular = false;
};

inline bool overlaps_exit(const ExitVolume& exit, Point feet, float radius = 35.0f) {
    const float dx = feet.x - exit.center.x, dz = feet.z - exit.center.z;
    // Grotto exits are infinite-height circles in the game; ordinary exits are rotated boxes.
    if (exit.circular) {
        const float reach = exit.halfX + radius;
        return dx * dx + dz * dz <= reach * reach;
    }
    if (feet.y + 150.0f < exit.center.y || feet.y > exit.center.y + exit.height) return false;
    const float x = dx * exit.cosine - dz * exit.sine;
    const float z = dx * exit.sine + dz * exit.cosine;
    const float ox = std::max(0.0f, std::fabs(x) - exit.halfX);
    const float oz = std::max(0.0f, std::fabs(z) - exit.halfZ);
    return ox * ox + oz * oz <= radius * radius;
}

// Sweep the whole movement, including rolls/jumps, so a thin loading strip cannot be tunnelled
// through. Stop just before contact; the game keeps its existing world collision and camera.
template<class Blocked>
Point sweep(Point from, Point to, Blocked blocked) {
    const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    const int steps = std::clamp(static_cast<int>(std::ceil(std::sqrt(dx * dx + dy * dy + dz * dz) / 20.0f)), 1, 512);
    Point allowed = from;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const Point candidate{from.x + dx * t, from.y + dy * t, from.z + dz * t};
        if (blocked(candidate)) break;
        allowed = candidate;
    }
    return allowed;
}

}  // namespace hs::arena
