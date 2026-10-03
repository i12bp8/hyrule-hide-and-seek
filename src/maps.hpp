#pragma once

// The Prop Hunt arenas. The data comes from tools/arena_builder.py, which reads the player's own
// disc: a central spawn on reachable floor, the play area, the disguises that belong on the map,
// extra native scenery (real pots, crates, furniture...) and the stage records that would start
// an event mid-round. arena.cpp applies them through StageService only while a round is played.

#include <cstdint>

namespace hs {

struct Circle {
    float x, z, radius;
};

// One native stage actor record (ACTR) to add for the round. Stored in the disc's big-endian
// layout by arena.cpp just before the warp.
struct Scenery {
    const char* name;
    uint32_t params;
    float x, y, z;
    int16_t angleX, angleY, angleZ;
    uint8_t room;
};

struct MapInfo {
    const char* name;
    const char* stage;
    int8_t room;
    int8_t layer;   // fixed post-story layer, or -1 where rooms differ (the game then chooses)
    int16_t point;  // custom spawn point id (> 255), added for the round
    int16_t fallbackPoint;  // native standing start, for Dusklight builds without StageService
    float spawnX, spawnY, spawnZ;
    int16_t spawnYaw;
    uint32_t spawnParams;  // copied from a native standing start in the same room
    bool large;
    float treasureStep;
    const Circle* area;
    uint8_t areaCount;
    const uint8_t* palette;  // PropId values, in D-pad order
    uint8_t paletteCount;
    const Scenery* scenery;
    uint16_t sceneryCount;
    const uint32_t* removed;  // CRC-32 of native records removed for the round
    uint16_t removedCount;
};

int map_count();
const MapInfo& map_info(int index);
// A random map index, different from `avoid` when there is a choice.
int random_map(int avoid, int players = 0);
int recommended_hunters(int players, int map);
// Inside the play area (horizontal union of circles).
bool in_area(const MapInfo& map, float x, float z, float margin = 0.0f);
// Distance to the play area's edge: positive inside, negative outside.
float area_edge_distance(const MapInfo& map, float x, float z);

}  // namespace hs
