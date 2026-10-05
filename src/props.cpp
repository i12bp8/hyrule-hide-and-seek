#include "props.hpp"

#include "maps.hpp"

#include <random>

namespace hs {

namespace {

// Archive, model and animation names were checked against the disc (tools/validate_prop_assets.py).
// Every look value is copied from the native actor's draw() and create():
//   light       dScnKy_env_light_c::settingTevStruct type (8 carryables, 0x10 scenery, 0 animals)
//   bgList      dComIfGd_setListBG() around the model
//   shadow      setSimpleShadow round/square (size = scale * native size) or setShadow (real)
//   btk/btp     the looping material animations the native actor enters every frame
constexpr PropRes res(const char* name) { return {name, -1}; }
constexpr PropRes res(int16_t index) { return {nullptr, index}; }
constexpr PropSolid cyl(float radius = 0.0f) { return {Solid::Cylinder, radius}; }
constexpr PropSolid mesh(PropRes dzb, float scale = 1.0f) { return {Solid::Background, 0.0f, dzb, scale}; }

// daObjCarry_c: light 8, round simple shadow of scale * mData[type].field_0x74 (the crate's is
// square and turns with it), l_cyl_info push radius.
constexpr PropInfo carry(const char* name, const char* arc, const char* bmd, float radius,
    float height, float scale, float shadow, float push, int type, Shadow kind = Shadow::Round) {
    return {.name = name, .arc = arc, .model = res(bmd), .radius = radius, .height = height,
        .scale = scale, .light = 8, .shadow = kind, .shadowSize = shadow, .solid = cyl(push),
        .native = Native::Carry, .nativeType = static_cast<int8_t>(type)};
}

// Castle Town pedestrians (daNpcCd2_c): one model per archive, Mgeneral/Wgeneral idle and walk,
// eye/mouth texture patterns, light 0 and a 40-unit round shadow under the feet.
constexpr PropInfo citizen(const char* name, const char* arc, const char* bmd, const char* btp,
    bool female) {
    return {.name = name, .arc = arc, .model = res(bmd),
        .idle = res(female ? "w_wait_a.bck" : "m_wait_a.bck"),
        .move = res(female ? "w_walk_a.bck" : "m_walk_a.bck"),
        .animArc = female ? "Wgeneral" : "Mgeneral", .btp = btp ? res(btp) : PropRes{},
        .radius = 45.0f, .height = 180.0f, .shadow = Shadow::Round, .shadowSize = 40.0f};
}

// daNi: base size 1.2, light 0, ni.btk frame = feather colour (white, black, brown; gold is the
// rare golden cucco, not offered), real shadow 400 from 100 units up. A real cucco is pushed
// aside rather than blocking Link, so the disguise is not solid either.
constexpr PropInfo cucco(const char* name, int frame) {
    return {.name = name, .arc = "Ni", .model = res(16), .idle = res(11), .move = res(12),
        .btk = res(19), .btkFrame = static_cast<int8_t>(frame), .radius = 42.0f, .height = 72.0f,
        .scale = 1.2f, .shadow = Shadow::Real, .shadowSize = 400.0f, .shadowLift = 100.0f,
        .native = Native::Cucco, .nativeType = static_cast<int8_t>(frame)};
}

// daNpc_Ne / daDo: light 0, btk frame = one of four coats, btp = blinking eyes, real shadow 400
// from 100 units up.
constexpr PropInfo cat(const char* name, int frame) {
    return {.name = name, .arc = "Npc_ne", .model = res(28), .idle = res(24), .move = res(25),
        .btk = res(32), .btkFrame = static_cast<int8_t>(frame), .btp = res(35), .radius = 30.0f,
        .height = 55.0f, .shadow = Shadow::Real, .shadowSize = 400.0f, .shadowLift = 100.0f,
        .native = Native::Cat, .nativeType = static_cast<int8_t>(frame)};
}

constexpr PropInfo dog(const char* name, int frame) {
    return {.name = name, .arc = "Do", .model = res(25), .idle = res(21), .move = res(22),
        .btk = res(29), .btkFrame = static_cast<int8_t>(frame), .btp = res(32), .radius = 40.0f,
        .height = 70.0f, .shadow = Shadow::Real, .shadowSize = 400.0f, .shadowLift = 100.0f,
        .native = Native::Dog, .nativeType = static_cast<int8_t>(frame)};
}

constexpr PropInfo kProps[kPropCount] = {
    carry("Pot", "J_tubo_00", "j_tubo_00.bmd", 40, 75, 1.0f, 40, 30, 0),
    carry("Big Pot", "J_tubo_01", "j_tubo_01.bmd", 50, 110, 1.0f, 50, 50, 1),
    carry("Crate", "Kkiba_00", "j_hako_00.bmd", 40, 77, 0.5f, 43.5f, 37, 2, Shadow::Square),
    carry("Barrel", "J_taru00", "j_taru_00.bmd", 55, 120, 1.0f, 50, 50, 4),
    carry("Skull", "J_doku00", "j_doku_00.bmd", 35, 55, 1.0f, 40, 30, 5),
    carry("Red Pot", "K_tubo02", "k_tubo02.bmd", 40, 75, 1.0f, 40, 30, 7),
    carry("Big Blue Pot", "D_aotubo0", "d_aotubo00.bmd", 50, 110, 1.0f, 50, 50, 10),

    // daObj_Pumpkin_c: HIO scale 1.4, light 0, round shadow 50 (scale / native scale * 50).
    {.name = "Pumpkin", .arc = "pumpkin", .model = res("pumpkin.bmd"), .radius = 45.0f,
        .height = 70.0f, .scale = 1.4f, .shadow = Shadow::Round, .shadowSize = 50.0f,
        .solid = cyl(34.0f), .native = Native::Pumpkin},
    // daObj_Pleaf_c: light 0, real shadow (HIO 900, 20 units up).
    {.name = "Pumpkin Leaves", .arc = "J_Hatake", .model = res(3), .radius = 90.0f,
        .height = 50.0f, .shadow = Shadow::Real, .shadowSize = 900.0f, .shadowLift = 20.0f,
        .native = Native::PumpkinLeaves},
    cucco("White Cucco", 0),
    // daCow_c: light 0, cow.btp, real shadow 800.
    {.name = "Goat", .arc = "Cow", .model = res(31), .idle = res(26), .move = res(27),
        .btp = res(34), .radius = 110.0f, .height = 170.0f, .shadow = Shadow::Real,
        .shadowSize = 800.0f, .solid = cyl(100.0f), .native = Native::Goat},
    // daObjYobikusa_c: background lighting and list, no shadow, leaves swing with the wind.
    {.name = "Hawk Grass", .arc = "J_Tobi", .model = res("j_tobi.bmd"), .radius = 40.0f,
        .height = 100.0f, .light = 0x10, .bgList = true, .motion = Motion::Sway,
        .native = Native::CallGrass, .nativeType = 0},
    {.name = "Horse Grass", .arc = "J_Umak", .model = res("j_umakusa.bmd"), .radius = 40.0f,
        .height = 100.0f, .light = 0x10, .bgList = true, .motion = Motion::Sway,
        .native = Native::CallGrass, .nativeType = 1},
    // daObj_Kanban2_c: light 0x10, real shadow 400.
    {.name = "Sign", .arc = "Obj_kn2", .model = res(3), .radius = 55.0f, .height = 150.0f,
        .light = 0x10, .shadow = Shadow::Real, .shadowSize = 400.0f, .solid = cyl(),
        .native = Native::Sign},
    {.name = "Nameplate", .arc = "J_Hyosatu", .model = res(3), .radius = 60.0f,
        .height = 175.0f, .offsetY = 171.7f, .light = 0x10, .bgList = true, .solid = cyl(),
        .native = Native::NamePlate},
    // daObjStone_c: light 8, round shadow l_shadow_size {40, 65}.
    {.name = "Small Rock", .arc = "D_Srock", .model = res(3), .radius = 47.0f, .height = 53.0f,
        .offsetY = 26.7f, .light = 8, .shadow = Shadow::Round, .shadowSize = 40.0f,
        .solid = cyl(27.0f), .native = Native::Stone, .nativeType = 0},
    {.name = "Big Rock", .arc = "D_Brock", .model = res(3), .radius = 73.0f, .height = 87.0f,
        .offsetY = 40.2f, .light = 8, .shadow = Shadow::Round, .shadowSize = 65.0f,
        .solid = cyl(43.0f), .native = Native::Stone, .nativeType = 1},
    // obj_lp: Obj_lp pads float on the water surface (dBgS_ObjGndChk_Spl) and bob.
    {.name = "Lily Pad", .arc = "Obj_lp", .model = res(3), .radius = 45.0f, .height = 40.0f,
        .scale = 1.2f, .motion = Motion::LilyPad, .native = Native::LilyPad},
    // daNpc_Kakashi_c: light 0, real shadow 700.
    {.name = "Scarecrow", .arc = "Kakashi", .model = res(10), .radius = 60.0f, .height = 190.0f,
        .shadow = Shadow::Real, .shadowSize = 700.0f, .shadowLift = 100.0f, .solid = cyl(40.0f),
        .native = Native::Scarecrow},
    // daObj_ItaMato_c / daObj_BouMato_c: light 0, real shadow 400 from 20 units up.
    {.name = "Board Target", .arc = "H_ItaMato", .model = res(4), .radius = 75.0f,
        .height = 170.0f, .shadow = Shadow::Real, .shadowSize = 400.0f, .shadowLift = 20.0f,
        .solid = mesh(res(8)), .native = Native::BoardTarget},
    {.name = "Pole Target", .arc = "H_BouMato", .model = res(4), .radius = 60.0f,
        .height = 292.0f, .shadow = Shadow::Real, .shadowSize = 400.0f, .shadowLift = 20.0f,
        .solid = cyl(40.0f), .native = Native::PoleTarget},

    {.name = "Gravestone", .arc = "H_Haka", .model = res(4), .radius = 133.0f, .height = 433.0f,
        .light = 0x10, .bgList = true, .solid = mesh(res(7)), .native = Native::GraveStone},
    // daObjMovebox::Act_c: background list, a square shadow at 40% that turns with the stone.
    {.name = "Pushable Grave", .arc = "H_OsiHaka", .model = res(4), .radius = 106.0f,
        .height = 225.0f, .light = 0x10, .bgList = true, .shadow = Shadow::Square,
        .shadowSize = 90.0f, .shadowAlpha = -0.4f, .solid = mesh(res(7))},

    cat("Black & White Cat", 0),
    dog("Tan Dog", 0),

    citizen("Townsman", "MAN_a", "man_a.bmd", nullptr, false),
    citizen("Father", "MAD_a", "mad_a.bmd", nullptr, false),
    citizen("Boy", "MCN_a", "mcn_a.bmd", "mcn_a.btp", false),
    citizen("Old Man", "MON_a", "mon_a.bmd", "mon_a.btp", false),
    citizen("Tall Townsman", "MAN_b", "man_b.bmd", nullptr, false),
    citizen("Stout Townsman", "MAN_c", "man_c.bmd", "man_c.btp", false),
    citizen("Merchant", "MAS_a", "mas_a.bmd", "mas_a.btp", false),
    citizen("Young Man", "MBN_a", "mbn_a.bmd", "mbn_a.btp", false),
    citizen("Townsman (Blue)", "MAN_a2", "man_a2.bmd", "man_a2.btp", false),
    citizen("Father (Green)", "MAD_a2", "mad_a2.bmd", "mad_a2.btp", false),
    citizen("Boy (Red)", "MCN_a2", "mcn_a2.bmd", "mcn_a2.btp", false),
    citizen("Old Man (Brown)", "MON_a2", "mon_a2.bmd", "mon_a2.btp", false),
    citizen("Tall Townsman (Red)", "MAN_b2", "man_b2.bmd", nullptr, false),
    citizen("Stout Townsman (Blue)", "MAN_c2", "man_c2.bmd", "man_c2.btp", false),
    citizen("Merchant (Green)", "MAS_a2", "mas_a2.bmd", "mas_a2.btp", false),
    citizen("Young Man (Blue)", "MBN_a2", "mbn_a2.bmd", "mbn_a2.btp", false),
    citizen("Townswoman", "WAN_a", "wan_a.bmd", "wan_a.btp", true),
    citizen("Lady", "WAD_a", "wad_a.bmd", "wad_a.btp", true),
    citizen("Housewife", "MAT_a", "mat_a.bmd", nullptr, true),
    citizen("Girl", "WCN_a", "wcn_a.bmd", "wcn_a.btp", true),
    citizen("Old Woman", "WON_a", "won_a.bmd", "won_a.btp", true),
    citizen("Grandmother", "WGN_a", "wgn_a.bmd", "wgn_a.btp", true),
    citizen("Young Woman", "WAN_b", "wan_b.bmd", "wan_b.btp", true),
    citizen("Townswoman (Red)", "WAN_a2", "wan_a2.bmd", "wan_a2.btp", true),
    citizen("Lady (Blue)", "WAD_a2", "wad_a2.bmd", "wad_a2.btp", true),
    citizen("Housewife (Green)", "MAT_a2", "mat_a2.bmd", nullptr, true),
    citizen("Girl (Blue)", "WCN_a2", "wcn_a2.bmd", "wcn_a2.btp", true),
    citizen("Old Woman (Green)", "WON_a2", "won_a2.bmd", "won_a2.btp", true),
    citizen("Grandmother (Red)", "WGN_a2", "wgn_a2.bmd", "wgn_a2.btp", true),
    citizen("Young Woman (Green)", "WAN_b2", "wan_b2.bmd", "wan_b2.btp", true),

    // daBarDesk_c, daObjCRVLH_DW_c, daObjChest_c, daObjCRVFENCE_c: background light and list.
    {.name = "Bar Desk", .arc = "KHdesk", .model = res(4), .radius = 90.0f, .height = 110.0f,
        .light = 0x10, .bgList = true, .solid = mesh(res(7)), .native = Native::BarDesk},
    {.name = "Lantern Post", .arc = "CrvLH_Dw", .model = res(4), .radius = 50.0f,
        .height = 240.0f, .light = 0x10, .bgList = true, .solid = mesh(res(7)),
        .native = Native::LanternPost},
    // daObjHFtr_c: light 8, background list, collision mesh. Native type is getNameId().
    {.name = "Chair", .arc = "HChair", .model = res(4), .radius = 60.0f, .height = 170.0f,
        .light = 8, .bgList = true, .solid = mesh(res(7)), .native = Native::Furniture,
        .nativeType = 0},
    {.name = "Sofa", .arc = "HSofa", .model = res(4), .radius = 120.0f, .height = 160.0f,
        .light = 8, .bgList = true, .solid = mesh(res(7)), .native = Native::Furniture,
        .nativeType = 1},
    {.name = "Dining Table", .arc = "HTable", .model = res(4), .radius = 90.0f,
        .height = 110.0f, .light = 8, .bgList = true, .solid = mesh(res(7)),
        .native = Native::Furniture, .nativeType = 2},
    // daObjHBarrel_c: light 8, plain list, no shadow.
    {.name = "Yeto's Barrel", .arc = "HBarrel", .model = res(3), .radius = 55.0f,
        .height = 120.0f, .light = 8, .solid = cyl(50.0f), .native = Native::YetoBarrel},
    // daObjTable_c: background, plus a real shadow 500 from 100 units up.
    {.name = "Map Table", .arc = "Table", .model = res(5), .radius = 200.0f, .height = 191.0f,
        .light = 0x10, .bgList = true, .shadow = Shadow::Real, .shadowSize = 500.0f,
        .shadowLift = 100.0f, .solid = mesh(res(8)), .native = Native::MapTable},
    {.name = "Dresser", .arc = "H_Tansu", .model = res(4), .radius = 129.0f, .height = 200.0f,
        .light = 0x10, .bgList = true, .solid = mesh(res(7)), .native = Native::Dresser},
    {.name = "Caravan Fence", .arc = "CrvFence", .model = res(8), .radius = 145.0f,
        .height = 150.0f, .light = 0x10, .bgList = true, .solid = mesh(res(14)),
        .native = Native::CaravanFence},
    // daObjIBone_c: HIO scale 1.35, light 0, no shadow.
    {.name = "Boar Bones", .arc = "Obj_Ibone", .model = res("a_inobone.bmd"), .radius = 189.0f,
        .height = 95.0f, .scale = 1.35f, .solid = cyl(), .native = Native::BoarBones},
    // daObj_Oiltubo_c: light 8, the jar plus its animated oil surface, real shadow 800 from 120.
    {.name = "Oil Jar", .arc = "Obj_otubo", .model = res(4), .extra = res(5),
        .extraBtk = res(8), .radius = 61.0f, .height = 117.0f, .light = 8,
        .shadow = Shadow::Real, .shadowSize = 800.0f, .shadowLift = 120.0f, .solid = cyl(),
        .native = Native::OilJar},
    // daObjGpTaru_c: background light and list.
    {.name = "Big Barrel", .arc = "K_ktar00", .model = res(3), .radius = 70.0f, .height = 150.0f,
        .light = 0x10, .bgList = true, .solid = cyl(60.0f), .native = Native::BigBarrel},

    // The other coats and feather colours, in btk frame order.
    cucco("Black Cucco", 1),
    cucco("Brown Cucco", 2),
    cat("Calico Cat", 1),
    cat("Tabby Cat", 2),
    cat("Orange Cat", 3),
    dog("Patched Dog", 1),
    dog("Brown Dog", 2),
    dog("Black Dog", 3),

    // Treasure only: daItem rupee look with its colour BRK, played by the treasure renderer.
    {.name = "Treasure Rupee", .arc = "F_gD_rupy", .model = res(4), .radius = 20.0f,
        .height = 60.0f},
};

static_assert(sizeof(kProps) / sizeof(kProps[0]) == kPropCount);

std::mt19937& rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

}  // namespace

int prop_count() {
    return kPropCount;
}

const PropInfo& prop_info(int index) {
    return kProps[(index >= 0 && index < kPropCount) ? index : kPot];
}

bool prop_selectable(int index) {
    return index >= 0 && index < kPropCount && index != kTreasureRupee;
}

bool prop_on_map(int index, int map) {
    if (!prop_selectable(index)) return false;
    if (map < 0 || map >= map_count()) return true;
    const MapInfo& info = map_info(map);
    for (int i = 0; i < info.paletteCount; ++i) {
        if (info.palette[i] == index) return true;
    }
    return false;
}

int prop_count_for_map(int map) {
    if (map < 0 || map >= map_count()) return kPropCount - 1;
    return map_info(map).paletteCount;
}

int prop_for_map(int map, int ordinal) {
    const int count = prop_count_for_map(map);
    if (count <= 0) return kPot;
    ordinal %= count;
    if (ordinal < 0) ordinal += count;
    if (map < 0 || map >= map_count()) return ordinal;  // every selectable prop precedes the rupee
    return map_info(map).palette[ordinal];
}

int random_prop(int map) {
    const int count = prop_count_for_map(map);
    if (count <= 0) return kPot;
    return prop_for_map(map, std::uniform_int_distribution<int>(0, count - 1)(rng()));
}

int step_prop(int current, int map, int direction) {
    const int count = prop_count_for_map(map);
    if (count <= 0) return kPot;
    int ordinal = -1;
    for (int i = 0; i < count; ++i) {
        if (prop_for_map(map, i) == current) ordinal = i;
    }
    if (ordinal < 0) return prop_for_map(map, 0);
    return prop_for_map(map, ordinal + (direction >= 0 ? 1 : -1));
}

int prop_for_native(Native native, int subtype) {
    if (native == Native::None) return -1;
    for (int i = 0; i < kPropCount; ++i) {
        const PropInfo& p = kProps[i];
        if (p.native == native && (p.nativeType < 0 || p.nativeType == subtype)) return i;
    }
    return -1;
}

}  // namespace hs
