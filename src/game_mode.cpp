#include "game_mode.hpp"

#include "common.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "ui.hpp"

#include <mods/svc/game_mode.h>

#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "d/d_save.h"

#include <cstring>

namespace hs::game_mode {

namespace {

constexpr const char* kModeId = "com.i12bp8.hyrule_hide_and_seek.mode";
constexpr const char* kSaveName = "hyrule-hide-and-seek";
constexpr int kLobbyMap = 0;  // Ordon Village
constexpr uint64_t kSettleMs = 1500;

bool s_active = false;
bool s_warpPending = false;
bool s_openWindow = false;
uint64_t s_inWorldSince = 0;

void prepare_world() {
    // Same world for everyone: no twilight anywhere, so nobody turns into a wolf.
    for (int region = 0; region < 4; ++region) dComIfGs_onDarkClearLV(region);
    // Hunters need a sword; everyone wears the Hero's Clothes so tunic colours show.
    dComIfGs_setCollectSword(COLLECT_ORDON_SWORD);
    dComIfGs_setSelectEquipSword(dItemNo_SWORD_e);
    dComIfGs_setCollectShield(COLLECT_HYLIAN_SHIELD);
    dComIfGs_setSelectEquipShield(dItemNo_HYLIA_SHIELD_e);
    dComIfGs_setCollectClothes(KOKIRI_CLOTHES_FLAG);
    dComIfGs_setSelectEquipClothes(dItemNo_WEAR_KOKIRI_e);
    if (dComIfGs_getMaxLife() < 30) dComIfGs_setMaxLife(30);  // six hearts (five pieces each)
    dComIfGs_setLife(static_cast<u16>(dComIfGs_getMaxLifeGauge()));
}

ModResult on_activated(void*, ModError*) {
    s_active = true;
    mods::log::info("Hide & Seek game mode on (its own save file)");
    return MOD_OK;
}

ModResult on_deactivated(void*, ModError*) {
    s_active = false;
    s_warpPending = false;
    s_openWindow = false;
    return MOD_OK;
}

ModResult on_new_save_select(void*, GameModeNewSaveState* state, ModError*) {
    *state = GAME_MODE_STATE_PROCEED;
    return MOD_OK;
}

ModResult on_save_loaded(void*, ModError*) {
    prepare_world();
    s_warpPending = true;
    s_openWindow = true;
    s_inWorldSince = 0;
    return MOD_OK;
}

}  // namespace

void init() {
    if (svc_game_mode == nullptr) {
        mods::log::info("no game mode service: Hide & Seek works from any save");
        return;
    }
    GameModeDesc desc = GAME_MODE_DESC_INIT;
    desc.game_mode_id = kModeId;
    desc.full_name = "Hide & Seek";
    std::strncpy(const_cast<char*>(desc.save_name), kSaveName, sizeof(desc.save_name) - 1);
    desc.on_activated = on_activated;
    desc.on_deactivated = on_deactivated;
    desc.on_save_loaded = on_save_loaded;
    desc.on_new_save = on_save_loaded;
    desc.on_new_save_select = on_new_save_select;
    if (svc_game_mode->register_game_mode(mod_ctx, &desc) != MOD_OK) {
        mods::log::warn("could not register the Hide & Seek game mode");
    }
}

void update() {
    if (!s_active) return;
    if (!local::in_world() || dComIfGp_isEnableNextStage()) {
        s_inWorldSince = 0;
        return;
    }
    const uint64_t now = now_ms();
    if (s_inWorldSince == 0) s_inWorldSince = now;
    if (now - s_inWorldSince < kSettleMs) return;

    // Skip the story: a loaded Hide & Seek file starts in Ordon Village, unless a round is on.
    if (s_warpPending) {
        s_warpPending = false;
        const MapInfo& lobby = map_info(kLobbyMap);
        if (!match::in_round() && std::strncmp(local::stage(), lobby.stage, 8) != 0) {
            dComIfGp_setNextStage(lobby.stage, lobby.point, lobby.room, -1);
            s_inWorldSince = 0;
            return;
        }
    }
    if (s_openWindow) {
        s_openWindow = false;
        if (net::status() == net::Status::Offline) ui::open();
    }
}

bool active() {
    return s_active;
}

}  // namespace hs::game_mode
