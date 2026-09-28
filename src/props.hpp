#pragma once

// What a hider can turn into. Models come from the player's own game files at runtime.

#include <cstdint>

namespace hs {

struct PropInfo {
    const char* name;
    const char* arc;      // /res/Object/<arc>.arc
    const char* bmd;      // model inside it
    const char* idleBck;  // optional looping animation (cucco)
    const char* moveBck;  // optional animation while moving
    float radius;         // hit cylinder for hunters' swords
    float height;
    float scale;
};

int prop_count();
const PropInfo& prop_info(int index);
int random_prop();
// The prop that looks like a carryable object of this daObjCarry_c type, or -1.
int prop_for_carry_type(int carryType);

}  // namespace hs
