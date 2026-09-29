#include "props.hpp"

#include <random>

namespace hs {

namespace {

// Archive and file names checked against the disc. The carry types are daObjCarry_c::mType.
constexpr PropInfo kProps[] = {
    {"Pot", "J_tubo_00", "j_tubo_00.bmd", nullptr, nullptr, 40.0f, 75.0f, 1.0f},
    {"Big Pot", "J_tubo_01", "j_tubo_01.bmd", nullptr, nullptr, 50.0f, 110.0f, 1.0f},
    {"Crate", "Kkiba_00", "j_hako_00.bmd", nullptr, nullptr, 60.0f, 110.0f, 1.0f},
    {"Barrel", "J_taru00", "j_taru_00.bmd", nullptr, nullptr, 55.0f, 120.0f, 1.0f},
    {"Skull", "J_doku00", "j_doku_00.bmd", nullptr, nullptr, 35.0f, 55.0f, 1.0f},
    {"Pumpkin", "pumpkin", "pumpkin.bmd", nullptr, nullptr, 45.0f, 70.0f, 1.0f},
    {"Kakariko Pot", "K_tubo02", "k_tubo02.bmd", nullptr, nullptr, 40.0f, 75.0f, 1.0f},
    {"Small Crate", "Obj_kbox", "k_skiba_00.bmd", nullptr, nullptr, 50.0f, 80.0f, 1.0f},
    {"Rock", "Obj_rock", "a_trock.bmd", nullptr, nullptr, 60.0f, 80.0f, 1.0f},
    {"Cucco", "Ni", "ni.bmd", "ni_wait1.bck", "ni_walk_a.bck", 35.0f, 60.0f, 1.0f},
    {"Cannonball", "Y_ironbal", "Yironball.bmd", nullptr, nullptr, 40.0f, 80.0f, 1.0f},
    {"Deku Nut", "Obj_bkl", "K_hb00.bmd", nullptr, nullptr, 45.0f, 60.0f, 1.0f},
    {"Big Blue Pot", "D_aotubo0", "D_aotubo00.bmd", nullptr, nullptr, 50.0f, 110.0f, 1.0f},
    {"Twilight Pot", "O_tuboS", "O_tuboS_LV8.bmd", nullptr, nullptr, 40.0f, 75.0f, 1.0f},
    {"Big Twilight Pot", "O_tuboB", "O_tuboB_LV8.bmd", nullptr, nullptr, 50.0f, 110.0f, 1.0f},
    {"Sign", "Obj_kn2", "J_kanban00.bmd", nullptr, nullptr, 55.0f, 150.0f, 1.0f},
    {"Chair", "HChair", nullptr, nullptr, nullptr, 60.0f, 170.0f, 1.0f, 4},
    {"Sofa", "HSofa", nullptr, nullptr, nullptr, 120.0f, 160.0f, 1.0f, 4},
    {"Table", "HTable", nullptr, nullptr, nullptr, 90.0f, 110.0f, 1.0f, 4},
    {"Boar Bones", "Obj_Ibone", "A_InoBone.bmd", nullptr, nullptr, 140.0f, 70.0f, 1.0f},
    {"Gravestone", "H_Haka", "H_Haka.bmd", nullptr, nullptr, 80.0f, 180.0f, 1.0f},
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

}  // namespace

int prop_count() {
    return static_cast<int>(sizeof(kProps) / sizeof(kProps[0]));
}

const PropInfo& prop_info(int index) {
    return kProps[(index >= 0 && index < prop_count()) ? index : 0];
}

int random_prop() {
    static std::mt19937 engine{std::random_device{}()};
    return std::uniform_int_distribution<int>(0, prop_count() - 1)(engine);
}

int prop_for_carry_type(int carryType) {
    for (const CarryMap& m : kCarry) {
        if (m.carryType == carryType) return m.prop;
    }
    return -1;
}

}  // namespace hs
