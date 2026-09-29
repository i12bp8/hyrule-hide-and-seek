#include "props.hpp"

#include <random>

namespace hs {

namespace {

constexpr uint16_t bit(int map) {
    return static_cast<uint16_t>(1u << map);
}

constexpr uint16_t kAll = 0x7FFF;
constexpr uint16_t kOrdon = bit(0) | bit(1) | bit(2);
constexpr uint16_t kForest = bit(2) | bit(3) | bit(11) | bit(14);
constexpr uint16_t kKakariko = bit(4) | bit(5) | bit(12);
constexpr uint16_t kMountain = bit(4) | bit(5) | bit(6) | bit(13) | bit(14);
constexpr uint16_t kWater = bit(2) | bit(7) | bit(8) | bit(9);
constexpr uint16_t kSettled = bit(0) | bit(4) | bit(10) | bit(12);
constexpr uint16_t kRural = kOrdon | bit(3) | bit(4) | bit(12) | bit(14);
constexpr uint16_t kOpen = bit(1) | bit(3) | bit(6) | bit(13) | bit(14);
constexpr uint16_t kCastle = bit(10);
constexpr uint16_t kDesert = bit(13);

// Archive and file names checked against the disc. The carry types are daObjCarry_c::mType.
// Keep the original first 21 entries in place: their numeric IDs already exist in recordings and
// old clients. New entries are grouped by the part of Hyrule where they naturally belong.
// Scales are the ones the native actor draws the model at (its d_a_obj_*.cpp, or 1.0 when it uses
// the stage's default), so a disguise is exactly the size of the real object next to it. Hit
// radius, height and shadow follow the same scale. Crystal, Lava Rock and Icicle Rock have no
// single native size (per-placement or random) and keep their hand-picked scale.
constexpr PropInfo kProps[] = {
    {"Pot", "J_tubo_00", "j_tubo_00.bmd", nullptr, nullptr, 40.0f, 75.0f, 1.0f, -1, kAll,
        40.0f},
    {"Big Pot", "J_tubo_01", "j_tubo_01.bmd", nullptr, nullptr, 50.0f, 110.0f, 1.0f, -1,
        kAll, 50.0f},
    {"Crate", "Kkiba_00", "j_hako_00.bmd", nullptr, nullptr, 37.0f, 77.0f, 0.5f, -1, kAll,
        43.0f},
    {"Barrel", "J_taru00", "j_taru_00.bmd", nullptr, nullptr, 55.0f, 120.0f, 1.0f, -1, kAll,
        50.0f},
    {"Skull", "J_doku00", "j_doku_00.bmd", nullptr, nullptr, 35.0f, 55.0f, 1.0f, -1, kAll,
        40.0f},
    {"Pumpkin", "pumpkin", "pumpkin.bmd", nullptr, nullptr, 63.0f, 98.0f, 1.4f, -1, kAll,
        70.0f},
    {"Kakariko Pot", "K_tubo02", "k_tubo02.bmd", nullptr, nullptr, 40.0f, 75.0f, 1.0f, -1,
        kAll, 40.0f},
    {"Small Crate", "Obj_kbox", "k_skiba_00.bmd", nullptr, nullptr, 61.0f, 83.0f, 0.5f, -1,
        kAll, 61.0f},
    {"Rock", "Obj_rock", "a_trock.bmd", nullptr, nullptr, 67.0f, 128.0f, 0.5f, -1, kAll},
    {"Cucco", "Ni", "ni.bmd", "ni_wait1.bck", "ni_walk_a.bck", 42.0f, 72.0f, 1.2f, -1,
        kRural, 48.0f},
    {"Cannonball", "Y_ironbal", "yironball.bmd", nullptr, nullptr, 40.0f, 70.0f, 1.0f, -1,
        static_cast<uint16_t>(kKakariko | kCastle | kDesert), 40.0f, 0.0f, 35.0f},
    {"Deku Nut", "Obj_bkl", "k_hb00.bmd", nullptr, nullptr, 27.0f, 36.0f, 0.6f, -1, kForest,
        27.0f},
    {"Big Blue Pot", "D_aotubo0", "d_aotubo00.bmd", nullptr, nullptr, 50.0f, 110.0f, 1.0f,
        -1, kAll, 50.0f},
    {"Twilight Pot", "O_tuboS", "o_tubos_lv8.bmd", nullptr, nullptr, 40.0f, 75.0f, 1.0f, -1,
        kAll, 40.0f},
    {"Big Twilight Pot", "O_tuboB", "o_tubob_lv8.bmd", nullptr, nullptr, 50.0f, 110.0f, 1.0f,
        -1, kAll, 50.0f},
    {"Sign", "Obj_kn2", "j_kanban00.bmd", nullptr, nullptr, 55.0f, 150.0f, 1.0f, -1,
        static_cast<uint16_t>(kSettled | kRural | kForest)},
    {"Chair", "HChair", nullptr, nullptr, nullptr, 60.0f, 170.0f, 1.0f, 4,
        static_cast<uint16_t>(kSettled | kCastle), 0.0f},
    {"Sofa", "HSofa", nullptr, nullptr, nullptr, 120.0f, 160.0f, 1.0f, 4,
        static_cast<uint16_t>(kSettled | kCastle), 0.0f},
    {"Table", "HTable", nullptr, nullptr, nullptr, 90.0f, 110.0f, 1.0f, 4,
        static_cast<uint16_t>(kSettled | kCastle), 0.0f},
    {"Boar Bones", "Obj_Ibone", "a_inobone.bmd", nullptr, nullptr, 189.0f, 95.0f, 1.35f, -1,
        static_cast<uint16_t>(kOpen | kDesert), 0.0f, 0.0f, 43.5f},
    {"Gravestone", "H_Haka", "h_haka.bmd", nullptr, nullptr, 133.0f, 433.0f, 1.0f, -1,
        static_cast<uint16_t>(bit(4) | bit(5) | bit(12) | bit(13))},

    {"Bomb Flower", "Bombf", nullptr, nullptr, nullptr, 45.0f, 35.0f, 1.0f, 4,
        static_cast<uint16_t>(kForest | kMountain)},
    {"Bomb", "Bombf", nullptr, nullptr, nullptr, 35.0f, 55.0f, 1.0f, 3, kAll, 25.0f},
    {"Beehive", "E_nest", nullptr, nullptr, nullptr, 69.0f, 154.0f, 1.0f, 3,
        static_cast<uint16_t>(kRural | kForest), 62.0f, 0.0f, 147.5f},
    {"Boss Chest", "M_BBox", nullptr, nullptr, nullptr, 121.0f, 143.0f, 1.0f, 4,
        static_cast<uint16_t>(kMountain | kCastle | bit(11)), 0.0f},
    {"Dresser", "H_Tansu", nullptr, nullptr, nullptr, 129.0f, 200.0f, 1.0f, 4,
        static_cast<uint16_t>(kSettled | kCastle), 0.0f},
    // The crystal model is authored around a world-space actor origin. Cancel that root transform
    // and enlarge the tiny native cluster so it is a visible, player-sized disguise.
    {"Crystal", "H_Suisho", nullptr, nullptr, nullptr, 75.0f, 170.0f, 4.0f, 4,
        static_cast<uint16_t>(kWater | kMountain | bit(11)), 0.0f, 2092.417f, 110.404f,
        -3794.413f},
    {"Lily Pad", "M_hasu", nullptr, nullptr, nullptr, 129.0f, 36.0f, 1.0f, 4, kWater, 0.0f,
        0.0f, 164.0f},
    // Ice floes, the raft and seaweed only fit a player when shrunk well below the objects in the
    // world, which gives the disguise away. At their real size they are platforms and kelp forests,
    // so, like River Rock, they keep their IDs but are never offered.
    {"Large Ice", "V_Ice_l", "ice_l.bmd", nullptr, nullptr, 600.0f, 500.0f, 1.0f, -1, 0,
        0.0f, 0.0f, 422.0f},
    {"Small Ice", "V_Ice_s", "ice_s.bmd", nullptr, nullptr, 340.0f, 340.0f, 1.0f, -1, 0,
        0.0f, 0.0f, 263.0f},
    {"Raft", "M_Ikada", "m_ikada.bmd", nullptr, nullptr, 467.0f, 150.0f, 1.0f, -1, 0,
        0.0f, 0.0f, 34.0f},
    {"Seaweed", "M_kaisou", "m_kaisou.bmd", nullptr, nullptr, 244.0f, 733.0f, 2.0f, -1, 0,
        0.0f},
    // Native laundry hangs down from its origin. Put its lower edge on the floor and use the
    // authored size; the old 0.4 scale made it both tiny and almost entirely underground.
    {"Laundry", "J_Sentaku", "j_sentaku.bmd", nullptr, nullptr, 45.0f, 150.0f, 1.0f, -1,
        static_cast<uint16_t>(kSettled | kRural), 0.0f, 0.0f, 144.5f},
    {"Metal Crate", "L_mbox_00", nullptr, nullptr, nullptr, 107.0f, 143.0f, 1.0f, 4,
        static_cast<uint16_t>(kMountain | kCastle | kDesert), 0.0f},
    {"House Nameplate", "J_Hyosatu", "j_hyousatu.bmd", nullptr, nullptr, 67.0f, 175.0f, 1.0f,
        -1, kSettled, 0.0f, 0.0f, 171.7f},
    {"Oil Jar", "Obj_otubo", "x_oiltubo_00.bmd", nullptr, nullptr, 61.0f, 117.0f, 1.0f, -1,
        static_cast<uint16_t>(kKakariko | kCastle | kDesert), 61.0f},
    // Kept in the table so old packets retain their numeric meaning, but never selected: this is
    // a six-thousand-unit environment mesh rather than a portable river rock.
    {"River Rock", "RiverRock", "m_riverrock.bmd", nullptr, nullptr, 90.0f, 70.0f, 0.7f, -1,
        0},
    {"Well Cover", "H_Idohuta", nullptr, nullptr, nullptr, 162.0f, 62.0f, 1.0f, 4,
        static_cast<uint16_t>(kSettled | kRural), 0.0f, -141.0f, -2.0f, -5.0f},
    {"Howling Stone", "WindStone", nullptr, nullptr, nullptr, 178.0f, 400.0f, 1.0f, 4,
        static_cast<uint16_t>(kForest | kMountain | kWater)},
    {"Wooden Statue", "O_wood", nullptr, nullptr, nullptr, 30.0f, 60.0f, 1.0f, 4,
        static_cast<uint16_t>(kOrdon | kForest | kKakariko), 0.0f},
    {"Hawk Grass", "J_Tobi", "j_tobi.bmd", nullptr, nullptr, 56.0f, 106.0f, 1.0f, -1,
        static_cast<uint16_t>(kRural | kForest | kWater)},
    {"Horse Grass", "J_Umak", "j_umakusa.bmd", nullptr, nullptr, 56.0f, 106.0f, 1.0f, -1,
        static_cast<uint16_t>(kRural | kOpen)},
    {"Pole Target", "H_BouMato", nullptr, nullptr, nullptr, 100.0f, 292.0f, 1.0f, 4,
        static_cast<uint16_t>(kOrdon | kKakariko | kOpen)},
    // The native board target needs two models; its frame-only legacy ID is intentionally hidden.
    {"Board Target", "H_ItaMato", nullptr, nullptr, nullptr, 75.0f, 170.0f, 0.65f, 5, 0},
    {"Village Fence", "H_Saku", "h_saku.bmd", nullptr, nullptr, 122.0f, 117.0f, 1.0f, -1,
        static_cast<uint16_t>(kSettled | kRural), 0.0f, -120.0f, 0.0f, 15.0f},
    {"Large Box", "Obj_lbox", nullptr, nullptr, nullptr, 283.0f, 367.0f, 2.0f, 4, kAll, 0.0f},
    {"Pumpkin Leaves", "J_Hatake", nullptr, nullptr, nullptr, 300.0f, 78.0f, 1.0f, 3,
        static_cast<uint16_t>(kRural | kOpen)},
    // This archive's model is only the candle base; the visible flame is a native particle actor.
    {"Palace Candle", "P_PCNDL", nullptr, nullptr, nullptr, 45.0f, 145.0f, 0.8f, 4, 0},
    {"Sacred Stone", "WStoneF", nullptr, nullptr, nullptr, 100.0f, 212.0f, 1.0f, 4,
        static_cast<uint16_t>(kForest | bit(11))},
    {"Kakariko Boulder", "syourock", nullptr, nullptr, nullptr, 211.0f, 400.0f, 1.0f, 4,
        static_cast<uint16_t>(kKakariko | kMountain), 0.0f, 0.0f, 250.0f},
    // The selected native desert-fence model is a flat rail with actor-specific placement.
    {"Desert Fence", "P_Mfence", nullptr, nullptr, nullptr, 145.0f, 110.0f, 0.4f, 4, 0},
    {"Lava Rock", "M_VolcBal", nullptr, nullptr, nullptr, 75.0f, 100.0f, 1.5f, 3,
        static_cast<uint16_t>(bit(6) | kDesert), 0.0f, 0.0f, 40.0f},
    {"Mountain Rock", "D_Srock", nullptr, nullptr, nullptr, 47.0f, 53.0f, 1.0f, 3, kMountain,
        27.0f, 0.0f, 26.7f},
    {"Large Mountain Rock", "D_Brock", nullptr, nullptr, nullptr, 73.0f, 87.0f, 1.0f, 3,
        kMountain, 43.0f, 0.0f, 40.2f},
    {"Icicle Rock", "M_DRockHn", nullptr, nullptr, nullptr, 75.0f, 150.0f, 0.7f, 3,
        static_cast<uint16_t>(kWater | bit(6)), 0.0f, 0.0f, 109.0f},
    {"Castle Barrel", "HBarrel", nullptr, nullptr, nullptr, 55.0f, 120.0f, 1.0f, 3,
        static_cast<uint16_t>(kCastle | kKakariko), 50.0f},
    {"Pushable Grave", "H_OsiHaka", nullptr, nullptr, nullptr, 106.0f, 225.0f, 1.0f, 4,
        static_cast<uint16_t>(bit(4) | bit(5) | bit(12)), 90.0f},
    // The buoy is a four-model animated assembly; one component alone appears invisible/tiny.
    {"Lake Buoy", "buoy", nullptr, nullptr, nullptr, 60.0f, 145.0f, 0.7f, 3, 0},
    {"Map Table", "Table", nullptr, nullptr, nullptr, 200.0f, 191.0f, 1.0f, 5,
        static_cast<uint16_t>(kCastle | kSettled | bit(12))},
};

struct CarryMap {
    int carryType;
    int prop;
};

// daObjCarry_c types (d_a_obj_carry.cpp l_arcName order) that match a prop.
constexpr CarryMap kCarry[] = {
    {0, 0},   // small blue pot
    {1, 1},   // big red pot
    {2, 2},   // crate
    {4, 3},   // barrel
    {5, 4},   // skull
    {7, 6},   // small red pot
    {3, 10},  // cannonball
    {6, 11},  // Deku nut
    {10, 12}, // big blue pot
    {12, 13}, // small Twilight pot
    {13, 14}, // big Twilight pot
};

// Collision copied from the native actor for the same archive. Props that are not listed here
// can be walked through, like their native object (Cucco is left out: a real one is pushed aside,
// which an immovable disguise can't copy).
struct NamedSolid {
    const char* name;
    PropSolid solid;
};

constexpr PropSolid cyl(float radius = 0.0f) {
    return {Solid::Cylinder, radius};
}
constexpr PropSolid bg(int index, float sx = 1.0f, float sy = 1.0f, float sz = 1.0f) {
    return {Solid::Background, 0.0f, nullptr, index, sx, sy, sz};
}
constexpr PropSolid bg(const char* name, float sx = 1.0f, float sy = 1.0f, float sz = 1.0f) {
    return {Solid::Background, 0.0f, name, -1, sx, sy, sz};
}

constexpr NamedSolid kSolids[] = {
    // daObjCarry_c: l_cyl_info radius, correction cylinder.
    {"Pot", cyl(30.0f)},
    {"Big Pot", cyl(50.0f)},
    {"Crate", cyl(37.0f)},
    {"Barrel", cyl(50.0f)},
    {"Skull", cyl(30.0f)},
    {"Kakariko Pot", cyl(30.0f)},
    {"Cannonball", cyl(40.0f)},
    {"Deku Nut", cyl(24.0f)},
    {"Big Blue Pot", cyl(50.0f)},
    {"Twilight Pot", cyl(30.0f)},
    {"Big Twilight Pot", cyl(50.0f)},
    // Other actors whose collider corrects Link's position.
    {"Pumpkin", cyl()},
    {"Rock", cyl()},
    {"Sign", cyl()},
    {"Boar Bones", cyl()},
    {"Beehive", cyl()},
    {"House Nameplate", cyl()},
    {"Oil Jar", cyl()},
    {"Wooden Statue", cyl()},
    {"Pole Target", cyl()},
    {"Mountain Rock", cyl()},
    {"Large Mountain Rock", cyl()},
    {"Icicle Rock", cyl()},
    {"Castle Barrel", cyl()},
    // Actors that register a collision mesh. Obj_kbox's mesh is drawn without the model's 0.5
    // scale, and H_Saku's is stretched to twice the model's width.
    {"Small Crate", bg(7, 2.0f, 2.0f, 2.0f)},
    {"Chair", bg(7)},
    {"Sofa", bg(7)},
    {"Table", bg(7)},
    {"Gravestone", bg("h_haka.dzb")},
    {"Boss Chest", bg(7)},
    {"Dresser", bg(7)},
    {"Lily Pad", bg(7)},
    {"Metal Crate", bg(7)},
    {"Well Cover", bg(7)},
    {"Howling Stone", bg(7)},
    {"Village Fence", bg("h_saku.dzb", 2.0f, 1.0f, 1.0f)},
    {"Large Box", bg(7)},
    {"Sacred Stone", bg(7)},
    {"Kakariko Boulder", bg(8)},
    {"Pushable Grave", bg(7)},
    {"Map Table", bg(8)},
};

bool same_name(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

}  // namespace

const PropSolid& prop_solid(int index) {
    static const PropSolid none{};
    const char* name = prop_info(index).name;
    for (const NamedSolid& s : kSolids) {
        if (same_name(s.name, name)) return s.solid;
    }
    return none;
}

int prop_solid_unmatched() {
    int unmatched = 0;
    for (const NamedSolid& s : kSolids) {
        bool found = false;
        for (int i = 0; i < prop_count() && !found; ++i) found = same_name(s.name, kProps[i].name);
        unmatched += found ? 0 : 1;
    }
    return unmatched;
}

int prop_count() {
    return static_cast<int>(sizeof(kProps) / sizeof(kProps[0]));
}

const PropInfo& prop_info(int index) {
    return kProps[(index >= 0 && index < prop_count()) ? index : 0];
}

bool prop_on_map(int index, int map) {
    if (index < 0 || index >= prop_count()) return false;
    if (kProps[index].mapMask == 0) return false;
    if (map < 0 || map >= 15) return true;
    return (kProps[index].mapMask & bit(map)) != 0;
}

int prop_count_for_map(int map) {
    int count = 0;
    for (int i = 0; i < prop_count(); ++i) count += prop_on_map(i, map) ? 1 : 0;
    return count;
}

int prop_for_map(int map, int ordinal) {
    const int count = prop_count_for_map(map);
    if (count <= 0) return 0;
    ordinal %= count;
    if (ordinal < 0) ordinal += count;
    for (int i = 0; i < prop_count(); ++i) {
        if (prop_on_map(i, map) && ordinal-- == 0) return i;
    }
    return 0;
}

int random_prop(int map) {
    static std::mt19937 engine{std::random_device{}()};
    const int count = prop_count_for_map(map);
    return prop_for_map(map, std::uniform_int_distribution<int>(0, count - 1)(engine));
}

int step_prop(int current, int map, int direction) {
    if (direction == 0 || prop_count() <= 0) return current;
    const int step = direction > 0 ? 1 : -1;
    int candidate = current;
    for (int i = 0; i < prop_count(); ++i) {
        candidate = (candidate + step + prop_count()) % prop_count();
        if (prop_on_map(candidate, map)) return candidate;
    }
    return prop_on_map(current, map) ? current : 0;
}

int prop_for_carry_type(int carryType) {
    for (const CarryMap& m : kCarry) {
        if (m.carryType == carryType) return m.prop;
    }
    return -1;
}

}  // namespace hs
