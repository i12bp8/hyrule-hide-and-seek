#pragma once

#include "arena_geometry.hpp"
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace hs::treasure {
using Point = arena::Point;

// Explore connected ground in bounded batches, before the hunt as well as during it. The game
// supplies ground/path checks; the layout itself has no dependency on player proximity or SDK.
class ReachableArea {
public:
    explicit ReachableArea(float step = 400.0f) : m_step(step) {}
    void clear(float step) { m_step = step; m_points.clear(); m_seen.clear(); m_next = 0; }

    template<class Ground>
    void seed(Point at, Ground ground) {
        const Point origin = at;
        if (ground(origin, at)) add(at);
    }

    template<class Ground>
    void expand(size_t budget, Ground ground) {
        constexpr int directions[][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {-1,1}, {1,-1}, {-1,-1}};
        while (budget-- > 0 && m_next < m_points.size() && m_points.size() < kMaxPoints) {
            const Point origin = m_points[m_next++];
            for (const auto& dir : directions) {
                Point at{origin.x + dir[0] * m_step, origin.y, origin.z + dir[1] * m_step};
                if (m_seen.contains(key(at))) continue;
                if (ground(origin, at)) add(at);
            }
        }
    }

    const std::vector<Point>& points() const { return m_points; }
    bool complete() const { return m_next >= m_points.size() || m_points.size() >= kMaxPoints; }

private:
    // Coordinates accepted by the match fit within +/-1,000,000. Signed cell indices fit in
    // 21 bits; height is retained so bridges and paths below them remain separate candidates.
    uint64_t key(Point p) const {
        const auto cell = [](float value, float unit) {
            return static_cast<uint64_t>(static_cast<int64_t>(std::llround(value / unit))) & 0x1FFFFF;
        };
        return cell(p.x, m_step) | (cell(p.z, m_step) << 21) | (cell(p.y, 200.0f) << 42);
    }
    void add(Point p) {
        if (m_points.size() < kMaxPoints && m_seen.insert(key(p)).second) m_points.push_back(p);
    }
    static constexpr size_t kMaxPoints = 65536;
    float m_step;
    size_t m_next = 0;
    std::vector<Point> m_points;
    std::unordered_set<uint64_t> m_seen;
};

inline float nearest_distance_sq(Point at, const std::vector<Point>& occupied) {
    float distance = std::numeric_limits<float>::max();
    for (const auto& p : occupied) {
        const float dx = at.x - p.x, dz = at.z - p.z;
        distance = std::min(distance, dx * dx + dz * dz);
    }
    return distance;
}

// Examine all explored ground, rather than a few candidates around a player. A random starting
// index breaks ties and changes the layout between rounds; distance spreads every refill out.
// Skip blocked locations during the search so a recent pickup cannot monopolize the best score.
template<class Allowed>
size_t spread_candidate(const std::vector<Point>& points, const std::vector<Point>& occupied,
                        size_t start, float spacing, Allowed allowed) {
    size_t best = points.size();
    float farthest = spacing * spacing;
    for (size_t n = 0; n < points.size(); ++n) {
        const size_t i = (start + n) % points.size();
        const float distance = nearest_distance_sq(points[i], occupied);
        if (distance >= farthest && allowed(points[i])) { best = i; farthest = distance; }
    }
    return best;
}

inline size_t spread_candidate(const std::vector<Point>& points, const std::vector<Point>& occupied,
                              size_t start, float spacing) {
    return spread_candidate(points, occupied, start, spacing, [](Point) { return true; });
}
}  // namespace hs::treasure
