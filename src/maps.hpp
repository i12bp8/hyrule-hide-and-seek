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
    // Exact position of the safe spawn above. Prop Hunt uses it as a shared, deterministic
    // centre for decorative decoys, so every client sees the same cover even on sparse maps.
    float spawnX;
    float spawnY;
    float spawnZ;
    float decoyRadius;
};

int map_count();
const MapInfo& map_info(int index);
// A random map index, different from `avoid` when there is a choice.
int random_map(int avoid);

}  // namespace hs
