#include "maps.hpp"

#include <cstdlib>
#include <random>

namespace hs {

namespace {

constexpr MapInfo kMaps[] = {
    {"Ordon Village", "F_SP103", 0, 13, -950.0f, 326.0f, 5400.0f},
    {"Ordon Ranch", "F_SP00", 0, 2, -4250.0f, 15302.0f, -19700.0f},
    {"Ordon Spring", "F_SP104", 1, 200, -2250.0f, 259.0f, -9600.0f},
    {"South Faron Woods", "F_SP108", 0, 3, -15600.0f, 0.0f, 200.0f},
    {"Kakariko Village", "F_SP109", 0, 14, -1885.0f, 0.0f, 7754.0f},
    {"Kakariko Graveyard", "F_SP111", 0, 4, 13867.0f, 100.0f, 920.0f},
    {"Death Mountain Trail", "F_SP110", 3, 0, 1545.0f, -450.0f, -253.0f},
    {"Zora's Domain", "F_SP113", 0, 0, -986.0f, 25.0f, -240.0f},
    {"Upper Zora's River", "F_SP126", 0, 1, 3622.0f, 222.0f, 434.0f},
    {"Lake Hylia", "F_SP115", 0, 0, -105752.0f, -18482.0f, 51996.0f},
    {"Castle Town", "F_SP116", 0, 0, 2.0f, 0.0f, -2231.0f},
    {"Sacred Grove", "F_SP117", 1, 10, -8.0f, 1625.0f, -2153.0f},
    {"Hidden Village", "F_SP128", 0, 5, 5400.0f, 0.0f, -4000.0f},
    {"Gerudo Desert", "F_SP124", 0, 1, 4321.0f, -733.0f, 35341.0f},
    {"Hyrule Field", "F_SP121", 0, 8, 13950.0f, 1523.0f, 18650.0f},
};

// Every cluster is a deliberately placed pair beside native scenery (fields, walls, rock groups,
// grave rows, market edges), not in the middle of the routes through the room. `dx,dz` is the
// half-spacing between its two objects. The numeric prop IDs are the stable catalogue IDs from
// props.cpp; their order is part of the network format and is covered by tests.
struct CoverCluster {
    float x, y, z;
    float dx, dz;
    uint8_t first, second;
    int16_t yaw;
};

constexpr CoverCluster kCoverClusters[][6] = {
    // Ordon Village: pumpkin plots, signpost, chicken yard and grassy rock edges.
    {{2800, 125, 3820, 150, 45, 5, 46, -3640}, {1400, 125, 1270, 125, 70, 5, 46, -3640},
        {-2050, 520, 6620, 125, 65, 8, 40, 12743}, {-100, 300, 4470, 145, 0, 9, 7, 0},
        {20, 166, 1334, 135, 0, 15, 0, -32768}, {3700, 185, 2770, 135, -95, 8, 41, 0}},
    // Ordon Ranch: pairs follow the outside of the pasture rather than crossing its centre.
    {{-9600, 15550, -22650, 0, 175, 44, 3, 0}, {-8300, 15600, -24500, 160, 0, 8, 41, 0},
        {-6500, 15560, -23100, 145, 50, 3, 2, 0}, {-4400, 15560, -22650, 130, 70, 5, 46, 0},
        {-10400, 15320, -18400, 150, 0, 41, 9, 0}, {-6500, 15300, -19000, 140, -50, 0, 7, 0}},
    // Ordon Spring: natural stones and plants along the banks and the two clearing edges.
    {{1200, 220, -4800, 125, 80, 36, 8, -11468}, {-250, 300, -4450, 130, 55, 8, 40, 0},
        {-300, 310, -5500, 125, 60, 40, 21, 16384}, {1150, 160, -5400, 120, -75, 36, 41, 0},
        {-1550, 280, -9100, 145, 55, 9, 0, 0}, {-250, 350, -19000, 145, 55, 36, 29, 0}},
    // South Faron Woods: foliage groups hug the sides of the long forest road.
    {{-17100, 45, -1400, 120, 65, 40, 11, 0}, {-15700, 70, -1900, 125, 55, 41, 21, 0},
        {-16300, 430, 2800, 135, 60, 8, 40, 0}, {-14900, 340, 3600, 120, 70, 11, 21, 0},
        {-15700, 450, 5550, 130, 60, 36, 40, 0}, {-14650, 310, 5100, 125, -65, 8, 41, 0}},
    // Kakariko Village: existing pot/sign clusters and vegetation at both ends of town.
    {{-3850, 5, 12100, 135, 55, 6, 0, 3640}, {-3300, 5, 2600, 130, 50, 6, 7, 0},
        {1700, 120, 4250, 135, 55, 35, 6, 16384}, {-1670, 5, 4120, 135, -45, 15, 0, 14563},
        {-700, 20, -5800, 125, 60, 49, 40, 0}, {-4250, 460, -8250, 140, 45, 15, 33, 2730}},
    // Kakariko Graveyard: additions continue the grave rows and rocky perimeter.
    {{10800, -150, -20, 0, 145, 4, 8, 0}, {12000, -50, 250, 0, 150, 20, 8, -32768},
        {13000, 120, 40, 0, 155, 56, 35, 0}, {15100, 280, 100, 0, 155, 20, 33, -16384},
        {17900, 500, -40, 0, 160, 49, 10, -16384}, {21100, 520, 20, 0, 160, 20, 4, -16384}},
    // Death Mountain Trail: barrels stay by camps; rocks stay against the volcanic walls.
    {{1400, -450, 600, 145, 45, 52, 3, 0}, {0, -250, -3000, 135, 75, 51, 52, 8192},
        {-3300, 1900, -6200, 145, 55, 3, 33, 10558}, {-3500, -150, -1900, 135, 70, 53, 52, -5461},
        {4200, -970, -4700, 145, 65, 53, 8, 0}, {-300, 3650, -3150, 130, 50, 3, 2, 21299}},
    // Zora's Domain: compact pairs sit along the outer ledges, leaving the central route open.
    {{650, 25, -980, 120, 55, 12, 26, 0}, {-800, 20, -850, 125, 50, 36, 29, 0},
        {-550, 15, -1400, 125, 55, 28, 8, 0}, {-300, 10, -1950, 125, 50, 26, 12, 0},
        {-50, 5, -2500, 125, 55, 36, 29, 0}, {250, 0, -3000, 130, 50, 28, 8, 0}},
    // Upper Zora's River: riverside rocks/grass and sensible crate groups near buildings.
    {{-1400, 340, -2700, 120, 60, 36, 29, 0}, {500, 240, -3500, 125, 55, 36, 40, 0},
        {2500, 540, -4400, 130, 60, 8, 40, 0}, {5000, 150, -3500, 135, 50, 36, 7, 0},
        {6800, 150, -2350, 135, 55, 2, 0, 0}, {6000, 80, 1500, 125, 55, 7, 40, 0}},
    // Lake Hylia: six shoreline areas, never on the lake floor where a prop would disappear.
    {{-108700, -18700, 50550, 145, 55, 40, 36, 21000},
        {-106800, -18740, 53900, 140, 60, 36, 29, 0},
        {-102800, -17180, 59200, 145, 55, 2, 40, 0},
        {-97000, -17800, 53800, 140, 60, 36, 8, 0},
        {-96500, -17550, 61200, 140, 55, 29, 40, 0},
        {-79000, -18800, 41400, 145, 60, 36, 40, 0}},
    // Castle Town: furniture is tucked against alternating sides of the broad main street.
    {{-1700, 760, -850, 0, 150, 55, 0, -8192}, {1700, 760, -850, 0, 150, 55, 6, 8192},
        {-1450, 760, -1750, 0, 155, 16, 47, -8192}, {1450, 760, -1750, 0, 155, 18, 47, 8192},
        {-700, 760, -2650, 0, 150, 25, 0, -16384}, {700, 760, -2650, 0, 150, 58, 55, 16384}},
    // Sacred Grove: symmetrical pairs sit beside, rather than across, the central forest route.
    {{-650, 1000, 1800, 0, 150, 48, 8, 0}, {650, 1000, 3000, 0, 150, 8, 11, -32768},
        {-650, 1300, 800, 0, 145, 21, 40, 0}, {650, 1625, -2200, 0, 145, 26, 8, 0},
        {-650, 1000, 5600, 0, 150, 48, 40, 0}, {650, 1725, 7600, 0, 150, 4, 8, 14563}},
    // Hidden Village: barrels, desks and fences extend native groups beside the buildings.
    {{1800, 0, -5100, 135, 55, 55, 3, 0}, {4400, 0, -3500, 140, 50, 58, 55, 0},
        {4400, 0, -6150, 140, 55, 18, 2, 0}, {2700, 105, -1450, 140, 55, 55, 7, 0},
        {3000, 430, -6500, 155, 0, 44, 3, -16384}, {1000, 480, -3800, 135, 55, 9, 40, 0}},
    // Gerudo Desert: camp clutter stays by ruins/fences and rock/bone groups stay in the wastes.
    {{4000, -730, 25500, 145, 60, 19, 50, -8556}, {3000, -730, 18300, 150, 55, 50, 52, 0},
        {4300, -730, 29500, 150, 55, 50, 19, 1820}, {9500, -730, 18300, 145, 60, 50, 51, -3640},
        {2900, -730, 46000, 140, 55, 4, 19, -32768}, {66200, 150, 56850, 145, 60, 4, 33, 0}},
    // Hyrule Field: widely separated roadside pairs; nothing is piled at the round spawn.
    {{34800, -300, -37250, 155, 55, 52, 41, 0}, {5315, 4440, -61600, 150, 60, 15, 41, 0},
        {14600, 1800, 21050, 155, 55, 44, 46, 0}, {-11600, -1030, 21550, 150, 60, 8, 41, 0},
        {34800, -300, -26750, 155, 55, 50, 19, 0}, {34800, -300, -32000, 150, 60, 52, 46, 0}},
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

int cover_point_count(int map) {
    return map >= 0 && map < map_count() ? kCoverPointCount : 0;
}

CoverPoint cover_point(int map, int ordinal) {
    if (map < 0 || map >= map_count()) map = 0;
    ordinal %= kCoverPointCount;
    if (ordinal < 0) ordinal += kCoverPointCount;
    const CoverCluster& cluster = kCoverClusters[map][ordinal / 2];
    const bool second = (ordinal & 1) != 0;
    const float side = second ? 1.0f : -1.0f;
    return {cluster.x + cluster.dx * side, cluster.y, cluster.z + cluster.dz * side,
        second ? cluster.second : cluster.first,
        static_cast<int16_t>(cluster.yaw + (second ? 0x0800 : 0))};
}

int random_map(int avoid) {
    const int n = map_count();
    std::uniform_int_distribution<int> pick(0, n - 1);
    int m = pick(rng());
    if (n > 1 && m == avoid) m = (m + 1 + std::uniform_int_distribution<int>(0, n - 2)(rng())) % n;
    return m;
}

}  // namespace hs
