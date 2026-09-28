#pragma once

// The local player: reads Link's state for the network, warps to the round's map, holds hunters
// still during the hide phase, hides Link while disguised, and handles the prop / taunt / sword
// controls.

#include <cstdint>

namespace hs::local {

bool init();  // installs hooks
void shutdown();
void update();  // every frame

bool in_world();           // Link exists and the stage is loaded
const char* stage();       // "" when not in the world
bool disguised();          // drawn as a prop right now
int prop();                // current prop kind
bool blindfolded();        // hunter waiting during the hide phase
float taunt_cooldown();    // 0..1, for the HUD
bool has_sword();

// A hunter's sword connected with a hider this swing (no miss penalty).
void note_hit();

// Plays a taunt sound at a player's position (hooked up to match).
void play_taunt(uint8_t from, uint8_t sound);

}  // namespace hs::local
