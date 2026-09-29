#pragma once

// What a hider can turn into. Models come from the player's own game files at runtime.

#include <cstdint>

namespace hs {

struct PropInfo {
    const char* name;
    const char* arc;      // /res/Object/<arc>.arc
    const char* bmd;      // model inside it; nullptr uses bmdIndex
    const char* idleBck;  // optional looping animation (cucco)
    const char* moveBck;  // optional animation while moving
    float radius;         // hit cylinder for hunters' swords
    float height;
    float scale;
    int bmdIndex = -1;    // archives whose model has no stable filename in the headers
    uint16_t mapMask = 0x7FFF;  // one bit per entry in maps.cpp
    // Multiplier for the generic round blob shadow. Zero lets flat, translucent or water props use
    // only the map's lighting instead of advertising themselves with an impossible black circle.
    float shadowScale = 1.0f;
};

int prop_count();
const PropInfo& prop_info(int index);
// Map-aware selection keeps Zora props near water, village furniture in settlements, and so on.
// A negative map selects from the full catalogue.
bool prop_on_map(int index, int map);
int prop_count_for_map(int map);
int prop_for_map(int map, int ordinal);
int random_prop(int map = -1);
int step_prop(int current, int map, int direction);
// The prop that looks like a carryable object of this daObjCarry_c type, or -1.
int prop_for_carry_type(int carryType);

}  // namespace hs
