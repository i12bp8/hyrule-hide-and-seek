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
    // A cheap circular shadow matching the native actor. Zero is intentional: model-projected
    // shadows submit the geometry a second time and can overflow Dusklight's per-frame FIFO.
    float simpleShadowSize = 0.0f;
    // Local-space correction for models authored around a hanging/off-centre native actor origin.
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float offsetZ = 0.0f;
};

int prop_count();
const PropInfo& prop_info(int index);
// Map-aware selection keeps Zora props near water, village furniture in settlements, and so on.
// A negative map selects from every enabled entry. A zero mapMask keeps a legacy network ID but
// prevents a composite or environment-sized model from being offered as a disguise.
bool prop_on_map(int index, int map);
int prop_count_for_map(int map);
int prop_for_map(int map, int ordinal);
int random_prop(int map = -1);
int step_prop(int current, int map, int direction);
// The prop that looks like a carryable object of this daObjCarry_c type, or -1.
int prop_for_carry_type(int carryType);

}  // namespace hs
