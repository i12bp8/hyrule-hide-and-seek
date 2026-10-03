#pragma once

// What a hider can turn into. Models come from the player's own game files at runtime.
//
// Every disguise copies the native actor that hunters see standing around the map: the same
// model and scale, the same lighting type and draw list, the same kind and size of shadow, the
// same looping material animations, the same wind sway and the same collision. A hunter should
// not be able to tell a hider from the real object by looking at it.

#include <cstdint>

namespace hs {

// Catalogue order is the wire ID (protocol 11). Append new props; never reorder.
enum PropId : uint8_t {
    // Carryable pots, crates and barrels (daObjCarry_c)
    kPot,
    kBigPot,
    kCrate,
    kBarrel,
    kSkull,
    kRedPot,
    kBlueBigPot,
    // Ordon and other farmland
    kPumpkin,
    kPumpkinLeaves,
    kCucco,
    kGoat,
    kHawkGrass,
    kHorseGrass,
    kSign,
    kNameplate,
    kSmallRock,
    kBigRock,
    kLilyPad,
    kScarecrow,
    kBoardTarget,
    kPoleTarget,
    // Kakariko
    kGravestone,
    kPushGrave,
    // Animals and townspeople
    kCat,
    kDog,
    kCitizenFirst,  // 30 Castle Town pedestrians follow
    kCitizenLast = kCitizenFirst + 29,
    // Settlements, ruins and camps
    kBarDesk,
    kLanternPost,
    kChair,
    kSofa,
    kDiningTable,
    kYetoBarrel,
    kMapTable,
    kDresser,
    kCaravanFence,
    kBoarBones,
    kOilJar,
    kBigBarrel,
    kTreasureRupee,  // renderer-only treasure model, never a selectable disguise
    kPropCount
};

// A resource inside an archive, by file name or by index (archives without stable names).
struct PropRes {
    const char* name = nullptr;
    int16_t index = -1;
    constexpr bool set() const { return name != nullptr || index >= 0; }
};

enum class Shadow : uint8_t {
    None,    // the native actor casts no shadow of its own (scenery baked into the room light)
    Round,   // dComIfGd_setSimpleShadow with the round simple texture
    Square,  // the same, without a texture: a square turned with the object (crates, blocks)
    Real,    // dComIfGd_setShadow: the model projected onto the ground
};

// The native actor class a disguise copies, for "become the object next to you". local.cpp maps the
// game's process names to these, keeping the catalogue independent of the game headers.
enum class Native : uint8_t {
    None, Carry, Pumpkin, PumpkinLeaves, Cucco, Goat, CallGrass, Sign, NamePlate, Stone, LilyPad,
    Scarecrow, BoardTarget, PoleTarget, GraveStone, Cat, Dog, BarDesk, LanternPost, Furniture,
    YetoBarrel, MapTable, Dresser, CaravanFence, BoarBones, OilJar, BigBarrel,
};

enum class Motion : uint8_t {
    None,
    Sway,     // Horse/Hawk Grass leaves move with the wind and bend when walked through
    LilyPad,  // floats on the water surface and bobs when someone moves nearby
};

// How the native object blocks Link. Disguises and decoys copy it so they are exactly as solid as
// the real thing: no more, no less.
enum class Solid : uint8_t {
    None,        // the native object can be walked through (grass, animals that are pushed aside)
    Cylinder,    // native push cylinder (pots, barrels, rocks)
    Background,  // native collision mesh (.dzb) from the same archive (furniture, fences, graves)
};

struct PropSolid {
    Solid kind = Solid::None;
    float radius = 0.0f;  // Cylinder: the native push radius; zero uses the hit radius
    PropRes dzb{};        // Background: collision file in the prop archive
    // Background: the native collision matrix relative to the model's, for archives whose .dzb
    // is authored at a different size than the .bmd.
    float bgScale = 1.0f;
};

struct PropInfo {
    const char* name;
    const char* arc;  // /res/Object/<arc>.arc
    PropRes model;
    PropRes extra{};     // second model drawn with the same matrix (oil surface)
    PropRes extraBtk{};  // its looping texture animation
    PropRes idle{};      // looping BCKs (animals and people)
    PropRes move{};
    const char* animArc = nullptr;  // archive holding the BCKs, when it isn't `arc`
    PropRes btk{};       // looping material animations, as the native actor plays them
    PropRes btp{};
    float radius = 40.0f;  // hunters' sword target
    float height = 80.0f;
    float scale = 1.0f;
    float offsetY = 0.0f;  // model origin above the feet
    uint8_t light = 0;     // dScnKy_env_light_c::settingTevStruct type of the native actor
    bool bgList = false;   // drawn in the background list, like furniture and scenery
    Shadow shadow = Shadow::None;
    float shadowSize = 0.0f;   // Round/Square: radius. Real: casting range
    float shadowLift = 0.0f;   // Real: shadow origin above the feet
    float shadowAlpha = 1.0f;  // Round/Square: native density parameter (negative = fixed alpha)
    Motion motion = Motion::None;
    PropSolid solid{};
    // Native actor this disguise copies. A negative type matches any sub-type; otherwise the
    // actor's own type (carry type, grass type, rock size, furniture kind).
    Native native = Native::None;
    int8_t nativeType = -1;
};

int prop_count();
const PropInfo& prop_info(int index);
bool prop_selectable(int index);  // a real disguise, not the treasure-only rupee

// Map palettes keep the disguise to things that actually stand around that map. Every map lists
// its own in maps.cpp; a disguise from another map would give the hider away immediately.
bool prop_on_map(int index, int map);
int prop_count_for_map(int map);
int prop_for_map(int map, int ordinal);
int random_prop(int map);
int step_prop(int current, int map, int direction);

// The prop that copies this native actor, or -1.
int prop_for_native(Native native, int subtype);

}  // namespace hs
