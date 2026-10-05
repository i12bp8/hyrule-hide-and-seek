#pragma once
#include "arena_geometry.hpp"

#include <cstdint>

namespace hs::arena {
void init();
void update();    // every frame: drops the round's stage edits once the room is back in the lobby
void shutdown();
// The only authorised round loading transition: registers the arena's stage edits (central spawn,
// extra scenery, removed event triggers), then loads the map.
void warp(int map);
// A plain warp to a native spawn point, for the game mode's lobby.
void warp_native(const char* stage, int16_t point, int8_t room);
bool locked();
bool overlaps_loading_exit(Point position, float radius = 100.0f);
}
