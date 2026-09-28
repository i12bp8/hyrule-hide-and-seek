#pragma once

// Other players in the world. One actor per remote player standing in our stage, drawn as Link in
// their tunic colour, or as their prop while disguised. The local player's own prop is drawn by
// the same actor type.

#include <cstdint>

struct cXyz;

namespace hs::puppet {

bool register_actor();
// Deletes every puppet and unregisters the actor profile. False if the game couldn't delete them
// right now (the caller must not free what they use).
bool unregister_actor();

// Spawns/removes puppets to match who is in our stage. Call once per frame.
void update();

// Where a player's puppet is drawn right now (interpolated), for name tags.
bool anchor(int id, cXyz& feet, float& height);

}  // namespace hs::puppet
