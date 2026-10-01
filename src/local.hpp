#pragma once

// The local player: reads Link's state for the network, warps to the round's map, holds hunters
// still during the hide phase, hides Link while disguised, and handles the prop / taunt / sword
// controls.

#include <cstdint>
#include "protocol.hpp"
#include "gameplay.hpp"

struct cXyz;

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
uint32_t tracking_cooldown_secs();
bool tracking_clue(SearchClue& clue);
uint32_t attack_recovery_ms();
ClueKind taunt_kind();

// A recent hider taunt as seen by a hunter. Returns its remaining 0..1 reveal strength.
float taunt_ping(int id, SearchClue& clue);

// A hunter's sword connected with a hider this swing (no miss penalty).
void note_hit();

// Plays and records a taunt at a player's position (hooked up to match).
void play_taunt(uint8_t from, uint8_t sound, ClueKind kind);

}  // namespace hs::local
