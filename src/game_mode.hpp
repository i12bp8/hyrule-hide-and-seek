#pragma once

// "Hide & Seek" on the title screen: its own save file, set up so every player's world matches
// (twilight cleared, Hero's Clothes, sword and shield, six hearts). Normal saves aren't touched.

namespace hs::game_mode {

void init();
void update();  // every frame
bool active();

}  // namespace hs::game_mode
