#include "maps.hpp"

#include <random>
#include <algorithm>
#include <vector>

namespace hs {

namespace {

constexpr MapInfo kMaps[] = {
    {"Ordon Village", "F_SP103", 0, 13, -950.0f, 326.0f, 5400.0f},
    {"Ordon Ranch", "F_SP00", 0, 2, -4250.0f, 15302.0f, -19700.0f},
    {"Ordon Spring", "F_SP104", 1, 200, -2250.0f, 259.0f, -9600.0f},
    {"South Faron Woods", "F_SP108", 0, 3, -15600.0f, 0.0f, 200.0f, true},
    {"Kakariko Village", "F_SP109", 0, 14, -1885.0f, 0.0f, 7754.0f},
    {"Kakariko Graveyard", "F_SP111", 0, 4, 13867.0f, 100.0f, 920.0f},
    {"Death Mountain Trail", "F_SP110", 3, 0, 1545.0f, -450.0f, -253.0f, true},
    {"Zora's Domain", "F_SP113", 0, 0, -986.0f, 25.0f, -240.0f, true},
    {"Upper Zora's River", "F_SP126", 0, 1, 3622.0f, 222.0f, 434.0f, true},
    {"Lake Hylia", "F_SP115", 0, 0, -105752.0f, -18482.0f, 51996.0f, true},
    {"Castle Town", "F_SP116", 0, 0, 2.0f, 0.0f, -2231.0f},
    {"Sacred Grove", "F_SP117", 1, 10, -8.0f, 1625.0f, -2153.0f},
    {"Hidden Village", "F_SP128", 0, 5, 5400.0f, 0.0f, -4000.0f},
    {"Gerudo Desert", "F_SP124", 0, 1, 4321.0f, -733.0f, 35341.0f, true},
    {"Hyrule Field", "F_SP121", 0, 8, 13950.0f, 1523.0f, 18650.0f, true},
};

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

uint64_t clue_interval_ms(int map, uint32_t remainingMs) {
    if (remainingMs <= 60000) return 10000;
    return map_info(map).large ? 20000 : 30000;
}

}  // namespace hs
