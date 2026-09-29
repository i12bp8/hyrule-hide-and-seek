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

// How the native object blocks Link. Disguises and decoys copy it so they are exactly as solid as
// the real thing: no more, no less.
enum class Solid : uint8_t {
    None,        // the native object can be walked through (grass, laundry, particles)
    Cylinder,    // native push cylinder (pots, barrels, rocks)
    Background,  // native collision mesh (.dzb) from the same archive (furniture, fences, chests)
};

struct PropSolid {
    Solid kind = Solid::None;
    float radius = 0.0f;         // Cylinder: the native push radius; zero uses the hit radius
    const char* dzb = nullptr;   // Background: collision file by name, or
    int dzbIndex = -1;           // by resource index when the archive has no stable name
    // Background: the native collision matrix relative to the model's, for archives whose .dzb
    // is authored at a different size than the .bmd.
    float bgScaleX = 1.0f;
    float bgScaleY = 1.0f;
    float bgScaleZ = 1.0f;
};

int prop_count();
const PropInfo& prop_info(int index);
const PropSolid& prop_solid(int index);
// Names in the collision table that match no prop. Zero unless someone renamed a prop.
int prop_solid_unmatched();
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
