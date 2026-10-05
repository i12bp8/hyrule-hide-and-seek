#include "maps.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace hs {

namespace {

#include "arena_data.inc"

std::mt19937& rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

}  // namespace

int map_count() {
    return static_cast<int>(sizeof(kMaps) / sizeof(kMaps[0]));
}

const MapInfo& map_info(int index) {
    return kMaps[(index >= 0 && index < map_count()) ? index : 0];
}

int random_map(int avoid, int players) {
    std::vector<int> choices;
    for (int m = 0; m < map_count(); ++m) {
        if (m == avoid || (players > 0 && players < 6 && map_info(m).large)) continue;
        choices.push_back(m);
    }
    if (choices.empty()) return 0;
    return choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(rng())];
}

int recommended_hunters(int players, int map) {
    if (players < 2) return 0;
    const int perHunter = map_info(map).large ? 3 : 4;
    return std::clamp((players + perHunter - 1) / perHunter, 1, players - 1);
}

}  // namespace hs
