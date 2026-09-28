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
};

struct CarryMap {
    int carryType;
    int prop;
};

// daObjCarry_c types (d_a_obj_carry.cpp l_arcName order) that match a prop.
constexpr CarryMap kCarry[] = {{0, 0}, {1, 1}, {2, 2}, {4, 3}, {5, 4}, {7, 6}};

}  // namespace

int prop_count() {
    return static_cast<int>(sizeof(kProps) / sizeof(kProps[0]));
}

const PropInfo& prop_info(int index) {
    return kProps[(index >= 0 && index < prop_count()) ? index : 0];
}

int random_prop() {
    static std::mt19937 engine{std::random_device{}()};
    // The first four are the most common objects in the world, so they're the best disguises.
    return std::uniform_int_distribution<int>(0, 3)(engine);
}

int prop_for_carry_type(int carryType) {
    for (const CarryMap& m : kCarry) {
        if (m.carryType == carryType) return m.prop;
    }
    return -1;
}

}  // namespace hs
