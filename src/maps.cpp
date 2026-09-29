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

// Six candidate areas in each selected room, based on safe stage coordinates. Three lightly offset
// decoys are derived from every anchor. The ground ray in puppet.cpp is authoritative, so a point
// whose nearby terrain is absent simply stays hidden rather than floating.
constexpr CoverPoint kCoverAnchors[][6] = {
    // Ordon Village
    {{-1670.0f, 725.0f, 7767.0f}, {-809.0f, 269.0f, 4654.0f},
        {-866.0f, 106.0f, 2325.0f}, {-1203.0f, 354.0f, -895.0f},
        {1904.0f, 98.0f, 1208.0f}, {1351.0f, 599.0f, 248.0f}},
    // Ordon Ranch
    {{-5000.0f, 15302.0f, -20700.0f}, {-4250.0f, 15302.0f, -19700.0f},
        {-3500.0f, 15302.0f, -18700.0f}, {3000.0f, 22818.0f, -29400.0f},
        {4019.0f, 22818.0f, -30397.0f}, {5000.0f, 22818.0f, -31400.0f}},
    // Ordon Spring
    {{-2900.0f, 233.0f, -9600.0f}, {-2250.0f, 259.0f, -9600.0f},
        {-8.0f, 402.0f, -18550.0f}, {-360.0f, 302.0f, -19661.0f},
        {-2600.0f, 245.0f, -9600.0f}, {-180.0f, 350.0f, -19100.0f}},
    // South Faron Woods, room 0
    {{-15600.0f, 0.0f, 200.0f}, {-15636.0f, 12.0f, 1278.0f},
        {-16145.0f, 0.0f, -209.0f}, {-15050.0f, 0.0f, 650.0f},
        {-15150.0f, 0.0f, -500.0f}, {-16200.0f, 0.0f, 850.0f}},
    // Kakariko Village
    {{-1885.0f, 0.0f, 7754.0f}, {-3200.0f, 3.0f, 2670.0f},
        {-350.0f, 562.0f, -5885.0f}, {2700.0f, 148.0f, 3700.0f},
        {-450.0f, 29.0f, 1650.0f}, {-3804.0f, 100.0f, 10786.0f}},
    // Kakariko Graveyard
    {{13867.0f, 100.0f, 920.0f}, {13200.0f, 50.0f, 720.0f},
        {12550.0f, 0.0f, 520.0f}, {11900.0f, -60.0f, 320.0f},
        {11250.0f, -115.0f, 120.0f}, {10632.0f, -170.0f, -59.0f}},
    // Death Mountain Trail, room 3
    {{1545.0f, -450.0f, -253.0f}, {-2667.0f, 84.0f, -4716.0f},
        {2800.0f, -1000.0f, -3400.0f}, {-416.0f, 3650.0f, -2863.0f},
        {800.0f, 900.0f, -1700.0f}, {-1450.0f, 1900.0f, -3750.0f}},
    // Zora's Domain, room 0
    {{-986.0f, 25.0f, -240.0f}, {-800.0f, 18.0f, -720.0f},
        {-600.0f, 12.0f, -1220.0f}, {-400.0f, 5.0f, -1730.0f},
        {-200.0f, 0.0f, -2240.0f}, {0.0f, -8.0f, -2750.0f}},
    // Upper Zora's River
    {{4909.0f, 150.0f, -4483.0f}, {3622.0f, 222.0f, 434.0f},
        {4205.0f, 138.0f, 784.0f}, {4074.0f, 150.0f, -4401.0f},
        {5829.0f, 150.0f, -3252.0f}, {2224.0f, 594.0f, -4068.0f}},
    // Lake Hylia
    {{-105752.0f, -18482.0f, 51996.0f}, {-108726.0f, -18704.0f, 50973.0f},
        {-101516.0f, -18470.0f, 53532.0f}, {-98426.0f, -17559.0f, 60055.0f},
        {-89121.0f, -18700.0f, 39880.0f}, {-77500.0f, -18679.0f, 41450.0f}},
    // Castle Town, room 0
    {{2.0f, 0.0f, -2231.0f}, {-750.0f, 0.0f, -1750.0f},
        {750.0f, 0.0f, -1750.0f}, {-900.0f, 0.0f, -2750.0f},
        {900.0f, 0.0f, -2750.0f}, {0.0f, 0.0f, -3350.0f}},
    // Sacred Grove, room 1
    {{0.0f, 1000.0f, 3065.0f}, {-8.0f, 1625.0f, -2153.0f},
        {0.0f, 1625.0f, -4472.0f}, {0.0f, 1725.0f, 7300.0f},
        {0.0f, 1300.0f, 800.0f}, {0.0f, 1675.0f, 5200.0f}},
    // Hidden Village
    {{3600.0f, -311.0f, -11000.0f}, {4000.0f, -220.0f, -9000.0f},
        {4500.0f, -120.0f, -7200.0f}, {4900.0f, -50.0f, -5600.0f},
        {5200.0f, -20.0f, -4700.0f}, {5400.0f, 0.0f, -4000.0f}},
    // Gerudo Desert
    {{4321.0f, -733.0f, 35341.0f}, {115.0f, 2345.0f, 57634.0f},
        {5364.0f, 514.0f, 55987.0f}, {15641.0f, 599.0f, 61043.0f},
        {1297.0f, -169.0f, 13039.0f}, {63203.0f, 451.0f, 50531.0f}},
    // Hyrule Field, room 0
    {{34800.0f, -299.0f, -37250.0f}, {5315.0f, 4442.0f, -61614.0f},
        {13950.0f, 1523.0f, 18650.0f}, {-11600.0f, -1034.0f, 21550.0f},
        {34800.0f, -299.0f, -26735.0f}, {34800.0f, -299.0f, -32000.0f}},
};

constexpr float kCoverOffsets[kCoverPointCount][2] = {
    {-170.0f, -90.0f}, {-120.0f, 160.0f}, {60.0f, -190.0f},
    {190.0f, 80.0f}, {-210.0f, 40.0f}, {100.0f, 190.0f},
    {160.0f, 100.0f}, {130.0f, -150.0f}, {-70.0f, 180.0f},
    {-180.0f, -70.0f}, {210.0f, -30.0f}, {-100.0f, -180.0f},
    {30.0f, 260.0f}, {-260.0f, -20.0f}, {230.0f, -180.0f},
    {-40.0f, -260.0f}, {270.0f, 120.0f}, {-220.0f, 190.0f},
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
    const CoverPoint& anchor = kCoverAnchors[map][ordinal % 6];
    return {anchor.x + kCoverOffsets[ordinal][0], anchor.y, anchor.z + kCoverOffsets[ordinal][1]};
}

int random_map(int avoid) {
    const int n = map_count();
    std::uniform_int_distribution<int> pick(0, n - 1);
    int m = pick(rng());
    if (n > 1 && m == avoid) m = (m + 1 + std::uniform_int_distribution<int>(0, n - 2)(rng())) % n;
    return m;
}

}  // namespace hs
