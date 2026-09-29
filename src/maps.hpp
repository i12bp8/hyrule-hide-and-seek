#pragma once

// The maps a round can be played on. Every spawn point was checked against the disc's stage
// files with tools/find_spawns.py (standing start, no event): a missing point crashes the game.

#include <cstdint>

namespace hs {

struct MapInfo {
    const char* name;
    const char* stage;
    int8_t room;
    int16_t point;
    // Exact position of the safe spawn above, retained as a fallback/reference point.
    float spawnX;
    float spawnY;
    float spawnZ;
};

struct CoverPoint {
    float x;
    float y;
    float z;
    uint8_t prop;
    int16_t yaw;
};

// Six deliberately authored scenery pairs per map: enough cover to break up empty spaces without
// making the hunter's starting area a junk pile.
constexpr int kCoverPointCount = 12;

int map_count();
const MapInfo& map_info(int index);
// Authored scenery placements based on the map's native actor layout. Each includes its exact prop
// and facing; layouts do not rotate or randomise between rounds. The actor's ground ray remains the
// final safety check.
int cover_point_count(int map);
CoverPoint cover_point(int map, int ordinal);
// A random map index, different from `avoid` when there is a choice.
int random_map(int avoid);

}  // namespace hs
