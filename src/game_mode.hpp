#pragma once

// "Hide & Seek" on the title screen: its own post-story save file, set up so every player's world
// matches and scripted events, encounters and hazards do not interrupt a round.

namespace hs::game_mode {

void init();
void update();  // every frame
bool active();
void prepare_stage();  // refresh persistent world state immediately before a round warp

}  // namespace hs::game_mode
