#include "game_mode.hpp"

#include "common.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "ui.hpp"

#include <mods/svc/game_mode.h>
#include <mods/svc/hook.hpp>

#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "d/d_save.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"

#include <cstring>

// Hook the actor wrapper rather than the global process executor. The latter also receives scenes,
// cameras and menus, so treating every process as fopAc_ac_c corrupts memory on file-select exit.
DEFINE_HOOK_SYMBOL("src/f_op/f_op_actor.cpp#fopAc_Execute", int(void*), HsActorExecute);
DEFINE_HOOK(&dComIfGp_event_order, HsEventOrder);

namespace hs::game_mode {

namespace {

constexpr const char* kModeId = "com.i12bp8.hyrule_hide_and_seek.mode";
constexpr const char* kSaveName = "hyrule-hide-and-seek";
constexpr int kLobbyMap = 0;  // Ordon Village
constexpr uint64_t kSettleMs = 1500;
constexpr f32 kNoon = 180.0f;

// Persistent event bits from a completed, late-game overworld. This selects the calm post-story
// versions of our maps and marks their story, side quests, tutorials and watched conversations as
// complete. It deliberately stops before the event-register area of the save.
//
// A few completion bits skipped by a normal 100% route are added here too (the Ordon tutorials,
// goat chase, Sacred Grove puppets and optional forced conversations on maps in our rotation).
// Current-state bits for wolf form, Midna riding, minigames and active cutscenes stay clear.
constexpr u8 kPostStoryEventBits[] = {
    /* 00 */ 0x88, 0x96, 0x40, 0x95, 0x0B, 0xFF, 0xFF, 0xAD,
    /* 08 */ 0xF5, 0x7E, 0x3F, 0x47, 0x4B, 0xF9, 0x43, 0xDF,
    /* 10 */ 0x17, 0x09, 0x1A, 0xA4, 0x98, 0x81, 0x3D, 0x00,
    /* 18 */ 0x01, 0x64, 0x0D, 0x40, 0x75, 0x80, 0xC8, 0xA0,
    /* 20 */ 0x3F, 0x82, 0x9F, 0xFF, 0xC0, 0xB2, 0xE0, 0x10,
    /* 28 */ 0x40, 0x6F, 0xE0, 0x08, 0x12, 0x00, 0x7E, 0x84,
    /* 30 */ 0x0E, 0x7F, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x05,
    /* 38 */ 0xC0, 0x0B, 0xFD, 0xF8, 0x0F, 0xD5, 0x1A, 0x00,
    /* 40 */ 0x88, 0x10, 0x6B, 0x0A, 0x00, 0x94, 0x91, 0x30,
    /* 48 */ 0x00, 0x02, 0x58, 0x04, 0x01, 0x88, 0x00, 0x00,
    /* 50 */ 0x81, 0x00, 0x30, 0x00, 0x30, 0x10, 0x00, 0x00,
    /* 58 */ 0x00, 0x68, 0x00, 0x0F, 0x20, 0x71, 0x9C, 0xB8,
    /* 60 */ 0x03, 0x0E, 0x38,
};

constexpr bool post_story_has(u16 flag) {
    const u16 byte = flag >> 8;
    return byte < sizeof(kPostStoryEventBits) &&
           (kPostStoryEventBits[byte] & static_cast<u8>(flag)) != 0;
}

static_assert(sizeof(kPostStoryEventBits) == 0x63);
static_assert(post_story_has(dSv_event_flag_c::M_001));   // opening watched
static_assert(post_story_has(dSv_event_flag_c::M_031));   // Goron Mines clear
static_assert(post_story_has(dSv_event_flag_c::F_0250));  // Midna's desperate hour complete
static_assert(post_story_has(dSv_event_flag_c::F_0354));  // Mirror of Twilight complete
static_assert(post_story_has(dSv_event_flag_c::F_0484));  // sky cannon repaired
static_assert(post_story_has(dSv_event_flag_c::F_0542));  // Hyrule Castle barrier gone
static_assert(post_story_has(dSv_event_flag_c::F_0570));  // Palace of Twilight clear
static_assert(post_story_has(dSv_event_flag_c::F_0749));  // Hidden Village cat game clear
static_assert(!post_story_has(dSv_event_flag_c::M_077));  // no voluntary wolf form
static_assert(!post_story_has(dSv_event_flag_c::T_0239)); // no live cannon/minigame state
static_assert(!post_story_has(dSv_event_flag_c::F_0776)); // no forced Palace wolf state

bool s_active = false;
bool s_saveLoaded = false;
bool s_worldSandbox = false;
bool s_eventsLocked = false;
bool s_warpPending = false;
bool s_openWindow = false;
uint64_t s_inWorldSince = 0;

bool sandbox_requested() {
    // A game mode is selected before file/name select and the title-screen attract scene. Do not
    // touch actors until its actual save is loaded, or mandatory title actors get mistaken for the
    // playable world.
    return (s_active && s_saveLoaded) ||
           (net::status() == net::Status::Online && match::in_round());
}

bool is_play_map(const char* stage) {
    if (stage == nullptr || stage[0] == '\0') return false;
    for (int i = 0; i < map_count(); ++i) {
        if (std::strncmp(stage, map_info(i).stage, 8) == 0) return true;
    }
    return false;
}

bool should_sandbox_current_stage() {
    if (s_active && s_saveLoaded) return is_play_map(local::stage());
    if (net::status() != net::Status::Online || !match::in_round()) return false;
    return std::strncmp(local::stage(), map_info(match::get().map).stage, 8) == 0;
}

// A few hostile helpers use the generic actor/environment group instead of the enemy group. The
// main actor still owns them, but removing them too prevents projectiles, nests and encounter
// effects from surviving for a frame after their parent is removed.
bool is_hostile_helper(s16 name) {
    switch (name) {
    case fpcNm_E_BEE_e:
    case fpcNm_E_BI_LEAF_e:
    case fpcNm_E_BUG_e:
    case fpcNm_E_DB_LEAF_e:
    case fpcNm_E_DF_e:
    case fpcNm_E_GA_e:
    case fpcNm_E_HB_LEAF_e:
    case fpcNm_E_MD_e:
    case fpcNm_E_MM_MT_e:
    case fpcNm_E_NEST_e:
    case fpcNm_E_TK_BALL_e:
    case fpcNm_E_WAP_e:
    case fpcNm_E_YD_LEAF_e:
    case fpcNm_E_YM_TAG_e:
    case fpcNm_E_ZH_e:
        return true;
    default:
        return false;
    }
}

bool should_suppress(fopAc_ac_c* actor) {
    if (actor == nullptr) return false;
    const s16 name = fopAcM_GetName(actor);
    if (fopAcM_GetGroup(actor) == fopAc_ENEMY_e || is_hostile_helper(name)) return true;

    // These actors turn an empty encounter into a switch, timer or cutscene. Without them,
    // removing enemies cannot accidentally complete a combat mission in the player's save.
    return name == fpcNm_ALLDIE_e || name == fpcNm_ECONT_e ||
           name == fpcNm_FORMATION_MNG_e || name == fpcNm_START_AND_GOAL_e ||
           name == fpcNm_TAG_ALLMATO_e;
}

int remove_hostile(void* raw, void*) {
    auto* actor = static_cast<fopAc_ac_c*>(raw);
    if (should_suppress(actor)) fopAcM_delete(actor);
    return 1;
}

HookAction on_actor_execute_pre(ModContext*, void* args, void* retval, void*) {
    if (!s_worldSandbox) return HOOK_CONTINUE;
    auto* actor = mods::arg<fopAc_ac_c*>(args, 0);
    if (!should_suppress(actor)) return HOOK_CONTINUE;
    fopAcM_delete(actor);
    if (retval != nullptr) *static_cast<int*>(retval) = 1;
    return HOOK_SKIP_ORIGINAL;
}

HookAction on_event_order_pre(ModContext*, void*, void* retval, void*) {
    if (!s_eventsLocked) return HOOK_CONTINUE;
    // Hide & Seek never needs a story, conversation, item-get or encounter event. Rejecting the
    // order before it starts avoids a one-frame boss intro and also makes NPCs/chests inert.
    if (retval != nullptr) *static_cast<int*>(retval) = 0;
    return HOOK_SKIP_ORIGINAL;
}

void prepare_world() {
    for (u16 byte = 0; byte < sizeof(kPostStoryEventBits); ++byte) {
        for (u16 bit = 1; bit <= 0x80; bit <<= 1) {
            if ((kPostStoryEventBits[byte] & bit) != 0) {
                dComIfGs_onEventBit(static_cast<u16>((byte << 8) | bit));
            }
        }
    }

    // Same world for everyone: no twilight or transformations, and always daylight.
    for (int region = 0; region < 4; ++region) {
        dComIfGs_onDarkClearLV(region);
        dComIfGs_offTransformLV(region);
    }
    dComIfGs_offEventBit(dSv_event_flag_c::M_067);   // Midna isn't riding
    dComIfGs_offEventBit(dSv_event_flag_c::M_077);   // transformation stays unavailable
    dComIfGs_offEventBit(dSv_event_flag_c::T_0239);  // not inside Fyer's cannon sequence
    dComIfGs_offEventBit(dSv_event_flag_c::F_0776);  // not forced into wolf form by Palace fog
    dComIfGs_setTransformStatus(TF_STATUS_HUMAN);
    dComIfGs_setTime(kNoon);

    // Prevent the Postman from starting a delivery event in the middle of a round. Twilight
    // Princess has 14 real letters; the remaining slots in the 64-entry table are empty.
    for (int letter = 0; letter < 14; ++letter) {
        dComIfGs_onLetterGetFlag(letter);
        dComIfGs_onLetterReadFlag(letter);
    }

    // Field rupees otherwise pause the first few pickups for an explanation banner.
    for (u8 item = dItemNo_GREEN_RUPEE_e; item <= dItemNo_SILVER_RUPEE_e; ++item) {
        dComIfGs_onItemFirstBit(item);
    }

    // Hunters need a sword; everyone wears the Hero's Clothes so tunic colours show.
    dComIfGs_setCollectSword(COLLECT_ORDON_SWORD);
    dComIfGs_setSelectEquipSword(dItemNo_SWORD_e);
    dComIfGs_onItemFirstBit(dItemNo_SWORD_e);
    dComIfGs_setCollectShield(COLLECT_HYLIAN_SHIELD);
    dComIfGs_setSelectEquipShield(dItemNo_HYLIA_SHIELD_e);
    dComIfGs_onItemFirstBit(dItemNo_HYLIA_SHIELD_e);
    dComIfGs_setCollectClothes(KOKIRI_CLOTHES_FLAG);
    dComIfGs_setSelectEquipClothes(dItemNo_WEAR_KOKIRI_e);
    dComIfGs_onItemFirstBit(dItemNo_WEAR_KOKIRI_e);
    if (dComIfGs_getMaxLife() < 30) dComIfGs_setMaxLife(30);  // six hearts (five pieces each)
    dComIfGs_setLife(static_cast<u16>(dComIfGs_getMaxLifeGauge()));
}

ModResult on_activated(void*, ModError*) {
    s_active = true;
    s_saveLoaded = false;
    s_worldSandbox = false;
    s_eventsLocked = false;
    mods::log::info("Hide & Seek game mode on (its own save file)");
    return MOD_OK;
}

ModResult on_deactivated(void*, ModError*) {
    s_active = false;
    s_saveLoaded = false;
    s_worldSandbox = false;
    s_eventsLocked = false;
    s_warpPending = false;
    s_openWindow = false;
    return MOD_OK;
}

ModResult on_new_save_select(void*, GameModeNewSaveState* state, ModError*) {
    *state = GAME_MODE_STATE_PROCEED;
    return MOD_OK;
}

ModResult on_save_loaded(void*, ModError*) {
    s_saveLoaded = true;
    s_worldSandbox = false;
    s_eventsLocked = false;
    prepare_world();
    s_warpPending = true;
    s_openWindow = true;
    s_inWorldSince = 0;
    return MOD_OK;
}

}  // namespace

void init() {
    if (mods::hook::add_pre<HsActorExecute>(on_actor_execute_pre) != MOD_OK) {
        mods::log::warn("enemy execution hook unavailable; using frame cleanup only");
    }
    if (mods::hook::add_pre<HsEventOrder>(on_event_order_pre) != MOD_OK) {
        mods::log::warn("event-order hook unavailable; completed save flags remain active");
    }
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
    s_worldSandbox = false;
    if (!sandbox_requested()) {
        s_eventsLocked = false;
        s_inWorldSince = 0;
        return;
    }
    if (!local::in_world() || dComIfGp_isEnableNextStage()) {
        s_eventsLocked = false;
        s_inWorldSince = 0;
        return;
    }

    const uint64_t now = now_ms();
    if (s_inWorldSince == 0) s_inWorldSince = now;
    s_worldSandbox = should_sandbox_current_stage();
    s_eventsLocked = s_worldSandbox && now - s_inWorldSince >= kSettleMs;

    if (s_worldSandbox) {
        // Run before the game's actor pass. Existing hostiles are deleted by its deletion pass,
        // while the execute hook catches anything created later in this same frame.
        fopAcIt_Executor(remove_hostile, nullptr);
        dComIfGp_setOxygen(dComIfGp_getMaxOxygen());
        if (s_eventsLocked && dComIfGp_event_runCheck()) dComIfGp_event_reset();
    }

    if (!s_active) return;

    // Rooms can opt back into the day/night clock while loading. Keep every client on the same
    // bright daytime palette and avoid night-only enemies changing a round underneath players.
    dComIfGs_setTime(kNoon);
    dComIfGp_roomControl_setTimePass(false);

    if (now - s_inWorldSince < kSettleMs) return;

    // Skip the story: a loaded Hide & Seek file starts in Ordon Village, unless a round is on.
    if (s_warpPending || (!match::in_round() && !is_play_map(local::stage()))) {
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

void prepare_stage() {
    if (s_active) prepare_world();
}

}  // namespace hs::game_mode
