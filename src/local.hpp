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
void set_prop(int kind);   // choose a disguise directly (test harness)
int16_t prop_yaw();        // the disguise's facing: a copied object's, or Link's
bool swap_charging(float& progress);  // D-pad up held towards a decoy swap
bool blindfolded();        // hunter waiting during the hide phase
float taunt_cooldown();    // 0..1, for the HUD
bool has_sword();
uint32_t tracking_cooldown_secs();
bool tracking_clue(SearchClue& clue);
ClueKind taunt_kind();

// A recent hider taunt as seen by a hunter, recalculated from the current view and positions.
// Returns its remaining 0..1 reveal strength.
float taunt_ping(int id, SearchClue& clue);

// Only the one final reveal gets an exact world marker. Ordinary taunts stay directional.
float final_clue_marker(int id, cXyz& position);

// A hunter's sword connected with a hider this swing (no miss penalty).
void note_hit();

// Plays and records a taunt at a player's position (hooked up to match).
void play_taunt(uint8_t from, uint8_t sound, ClueKind kind);
// A hunter whistled: every hidden prop makes a sound where it is.
void play_whistle(uint8_t hunter);
// A confirmed decoy swap moves the local Link.
void teleport(float x, float y, float z, int16_t yaw);

}  // namespace hs::local
