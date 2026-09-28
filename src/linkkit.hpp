#pragma once

// Everything needed to draw other players as Link, read from the player's own game files:
//
// - Link's Hero's Clothes models, copied out of Kmdl.arc once. The body and cap are loaded once
//   per tunic colour with the green recoloured in the texture data, so every player looks
//   different. These model datas are ours alone: nothing the local Link does touches them.
// - Sword, sheath and shield models.
// - Link's animations, read by index from the game's AlAnm archive and cached.
//
// All of it lives in one heap the mod creates from the root heap and frees on shutdown.

#include <cstdint>

class J3DModelData;
class J3DAnmTransform;
class JKRHeap;

namespace hs::linkkit {

struct LinkModels {
    J3DModelData* body = nullptr;
    J3DModelData* head = nullptr;  // cap and hair
    J3DModelData* face = nullptr;
    J3DModelData* hands = nullptr;
    J3DModelData* sword = nullptr;
    J3DModelData* sheath = nullptr;
    J3DModelData* shield = nullptr;
};

// False if the heap or Link's files could not be set up; puppets then draw nothing.
bool ready();
JKRHeap* heap();

// Loads on first use. nullptr if something is missing.
const LinkModels* link_models(uint8_t color);

// Link's animation by AlAnm index, cached. nullptr if it can't be loaded.
J3DAnmTransform* anim(uint16_t idx);

// The standing animation, used when a player's current one isn't known.
constexpr uint16_t kIdleAnim = 0x26A;  // dRes_ID_ALANM_BCK_WAITS_e

// Recolours the local player's own tunic (TextureService, so it follows the mod's lifecycle).
void recolor_local_link(uint8_t color);

void shutdown();

}  // namespace hs::linkkit
