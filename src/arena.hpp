#pragma once
#include "arena_geometry.hpp"

namespace hs { struct MapInfo; }
namespace hs::arena {
void init();
void warp(const MapInfo& map);  // the only authorised round/lobby loading transition
bool locked();
bool overlaps_loading_exit(Point position, float radius = 100.0f);
}
