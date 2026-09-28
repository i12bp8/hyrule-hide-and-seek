#include "maps.hpp"

#include <cstdlib>
#include <random>

namespace hs {

namespace {

constexpr MapInfo kMaps[] = {
    {"Ordon Village", "F_SP103", 0, 13},
    {"Ordon Ranch", "F_SP00", 0, 2},
    {"Ordon Spring", "F_SP104", 1, 200},
    {"South Faron Woods", "F_SP108", 0, 3},
    {"Kakariko Village", "F_SP109", 0, 14},
    {"Kakariko Graveyard", "F_SP111", 0, 4},
    {"Death Mountain Trail", "F_SP110", 3, 0},
    {"Zora's Domain", "F_SP113", 0, 0},
    {"Upper Zora's River", "F_SP126", 0, 1},
    {"Lake Hylia", "F_SP115", 0, 0},
    {"Castle Town", "F_SP116", 0, 0},
    {"Sacred Grove", "F_SP117", 1, 10},
    {"Hidden Village", "F_SP128", 0, 5},
    {"Gerudo Desert", "F_SP124", 0, 1},
    {"Hyrule Field", "F_SP121", 0, 8},
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

int random_map(int avoid) {
    const int n = map_count();
    std::uniform_int_distribution<int> pick(0, n - 1);
    int m = pick(rng());
    if (n > 1 && m == avoid) m = (m + 1 + std::uniform_int_distribution<int>(0, n - 2)(rng())) % n;
    return m;
}

}  // namespace hs
