#pragma once

namespace hs { struct MapInfo; }
namespace hs::arena {
void init();
void warp(const MapInfo& map);  // the only authorised round/lobby loading transition
bool locked();
}
