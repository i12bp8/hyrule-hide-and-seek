#include "local.hpp"

#include "common.hpp"
#include "arena.hpp"
#include "game_mode.hpp"
#include "gameplay.hpp"
#include "hud.hpp"
#include "linkkit.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "props.hpp"
#include "puppet.hpp"
#include "settings.hpp"

#include <mods/svc/hook.hpp>

#include "Z2AudioLib/Z2SeMgr.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_obj_carry.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_controller_pad.h"

#include <cmath>
#include <cstring>
#include <random>

DEFINE_HOOK(&daAlink_c::execute, HsLinkExecute);
DEFINE_HOOK(&daAlink_c::draw, HsLinkDraw);
DEFINE_HOOK(&daAlink_c::setCutType, HsLinkSetCutType);
DEFINE_HOOK(&daAlink_c::setDamagePoint, HsLinkDamage);
DEFINE_HOOK(&daAlink_c::checkNotBattleStage, HsCheckNotBattleStage);

namespace hs::local {

namespace {

constexpr uint64_t kDecoyCooldownMs = 750;
constexpr uint64_t kSwingWindowMs = 650;
constexpr uint64_t kSettleMs = 1500;
constexpr uint64_t kWarpRetryMs = 12000;
constexpr float kTouchDistance = 90.0f;
constexpr float kSwimTagBodyRadius = 45.0f;
constexpr float kSwimTagVerticalMargin = 80.0f;
constexpr float kCopyDistance = 300.0f;
constexpr u32 kRoundDpadMask =
    PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT;

// Link's own voice is always loaded, so these work on every map.
constexpr uint32_t kTaunts[] = {
    Z2SE_AL_V_ATTACK_L, Z2SE_AL_V_DAMAGE_COMIC, Z2SE_AL_V_JUMP_L,
    Z2SE_AL_V_FALL, Z2SE_AL_V_LIFTUP_L, Z2SE_SY_HINT_BUTTON,
};
constexpr int kTauntCount = static_cast<int>(sizeof(kTaunts) / sizeof(kTaunts[0]));

bool s_hooked = false;
char s_stage[9] = {};
bool s_inWorld = false;

// Round
uint32_t s_round = 0;
bool s_warpPending = false;
uint64_t s_warpedAt = 0;
uint32_t s_readyRound = 0;
uint64_t s_arrivedAt = 0;
Phase s_lastPhase = Phase::Lobby;

// Disguise and controls
bool s_disguised = false;
int s_prop = 0;
uint64_t s_lastTaunt = 0;
ClueKind s_myClueKind = ClueKind::Manual;
uint64_t s_attackReadyAt = 0;
uint8_t s_confirmedFinds = 0;
uint64_t s_lastDecoy = 0;

struct TauntPing {
    cXyz position{0.0f, 0.0f, 0.0f};
    uint64_t at = 0;
};
TauntPing s_tauntPings[kSlots];

// Hunters
bool s_frozen = false;
cXyz s_holdPos{0.0f, 0.0f, 0.0f};
bool s_swinging = false;
bool s_swingHit = false;
uint64_t s_swingAt = 0;
u8 s_lastCutType = 0;
uint64_t s_lastSwordCheck = 0;
uint64_t s_lastColorCheck = 0;
uint64_t s_lastTracking = 0;
bool s_haveTracking = false;
cXyz s_trackingPosition{0.0f, 0.0f, 0.0f};
bool s_roundHealth = false;
u8 s_previousMaxLife = 0;
u16 s_previousLife = 0;

std::mt19937 s_rng{std::random_device{}()};

daAlink_c* link() {
    fopAc_ac_c* p = dComIfGp_getPlayer(0);
    return p != nullptr && fopAcM_GetName(p) == fpcNm_ALINK_e ? static_cast<daAlink_c*>(p) : nullptr;
}

bool playing_prop_hunt() {
    return match::get().settings.mode == Mode::PropHunt;
}

const MapInfo& round_map() {
    return map_info(match::get().map);
}

// ---- hooks -----------------------------------------------------------------------------------

void on_link_execute_post(ModContext*, void* args, void*, void*) {
    if (!s_frozen) return;
    auto* self = mods::arg<daAlink_c*>(args, 0);
    self->current.pos = s_holdPos;
    self->old.pos = s_holdPos;
    self->speedF = 0.0f;
    self->speed.set(0.0f, 0.0f, 0.0f);
}

HookAction on_link_draw_pre(ModContext*, void*, void* retval, void*) {
    // Loading an archive takes a few frames. Keep Link as the fallback until the replacement has
    // completed an update; a missing model must never turn the local player invisible.
    if (!s_disguised || !puppet::local_prop_visible()) return HOOK_CONTINUE;
    if (retval != nullptr) *static_cast<int*>(retval) = 1;
    return HOOK_SKIP_ORIGINAL;
}

// Link draws every frame we're in the world, which is when the 2D lists are being filled.
void on_link_draw_post(ModContext*, void*, void*, void*) {
    hud::queue();
}

void on_set_cut_type_post(ModContext*, void* args, void*, void*) {
    const u8 type = mods::arg<u8>(args, 1);
    if (type == 0 || now_ms() < s_attackReadyAt) return;
    // A new swing: settle the previous one first.
    s_swinging = true;
    s_swingHit = false;
    s_swingAt = now_ms();
}

HookAction on_link_damage_pre(ModContext*, void* args, void* retval, void*) {
    // Enemy and environmental damage can otherwise kill a player while the network round keeps
    // going. The miss penalty writes hearts directly, so it still works.
    const int amount = mods::arg<int>(args, 1);
    const bool protectedPlay = game_mode::active() ||
                               (net::status() == net::Status::Online && match::in_round());
    if (amount <= 0 || !protectedPlay) {
        return HOOK_CONTINUE;
    }
    if (retval != nullptr) *static_cast<int*>(retval) = 0;
    return HOOK_SKIP_ORIGINAL;
}

HookAction on_not_battle_stage_pre(ModContext*, void*, void* retval, void*) {
    // Castle Town and ST_ROOM stages normally suppress B-button combat. Those restrictions are
    // story rules, not useful arena rules, and made a hunter's granted sword unusable there.
    if (net::status() != net::Status::Online || !match::in_round() ||
        match::my_role() != Role::Hunter) {
        return HOOK_CONTINUE;
    }
    if (retval != nullptr) *static_cast<bool*>(retval) = false;
    return HOOK_SKIP_ORIGINAL;
}

// ---- helpers ---------------------------------------------------------------------------------

void play_at(uint32_t sound, const cXyz* pos) {
    mDoAud_seStart(sound, pos, 0, 0);
}

void ensure_hunter_sword() {
    if (!dComIfGs_isCollectSword(COLLECT_ORDON_SWORD)) {
        dComIfGs_setCollectSword(COLLECT_ORDON_SWORD);
    }
    if (dComIfGs_getSelectEquipSword() != dItemNo_SWORD_e) {
        dComIfGs_setSelectEquipSword(dItemNo_SWORD_e);
    }
    // The live play-state copy can differ from the save-state copy after a stage transition.
    dComIfGp_setSelectEquipSword(dItemNo_SWORD_e);
    dComIfGs_onItemFirstBit(dItemNo_SWORD_e);
}

void read_state(daAlink_c* l, PlayerState& s) {
    s.flags = STATE_IN_WORLD;
    if (l->checkWolf()) s.flags |= STATE_WOLF;
    if (s_disguised) s.flags |= STATE_DISGUISED | STATE_COMPACT;
    if (l->mEquipItem == 0x103) s.flags |= STATE_SWORD | STATE_SHIELD;
    copy_str(s.stage, s_stage);
    s.room = static_cast<int8_t>(fopAcM_GetRoomNo(l));
    s.x = l->current.pos.x;
    s.y = l->current.pos.y;
    s.z = l->current.pos.z;
    s.yaw = l->shape_angle.y;
    s.prop = static_cast<uint8_t>(s_prop);
    s.propYaw = l->shape_angle.y;
    const auto slot = [](daPy_anmHeap_c& heap, mDoExt_AnmRatioPack& pack, AnimSlot& out) {
        // Animations from cutscene archives (arc no. set) aren't in AlAnm; skip them.
        const u16 idx = heap.checkNoSetPriIdx() ? heap.getIdx() : heap.mPriIdx;
        J3DAnmTransform* anm = pack.getAnmTransform();
        out.idx = heap.checkNoSetArcNo() && anm != nullptr ? idx : 0xFFFF;
        out.frame = anm != nullptr ? anm->getFrame() : 0.0f;
        out.ratio = static_cast<uint8_t>(std::clamp(pack.getRatio(), 0.0f, 1.0f) * 255.0f);
    };
    for (int i = 0; i < 3; ++i) {
        slot(l->mUnderAnmHeap[i], l->mNowAnmPackUnder[i], s.under[i]);
        slot(l->mUpperAnmHeap[i], l->mNowAnmPackUpper[i], s.upper[i]);
    }
}

// The carryable object nearest to Link, as a prop kind, or -1.
struct NearSearch {
    cXyz from;
    float best;
    int kind;
};

void* judge_carry(void* actor, void* data) {
    auto* a = static_cast<fopAc_ac_c*>(actor);
    auto* n = static_cast<NearSearch*>(data);
    if (fopAcM_GetName(a) != fpcNm_Obj_Carry_e) return nullptr;
    const float d = (a->current.pos - n->from).abs();
    if (d >= n->best) return nullptr;
    const int kind = prop_for_carry_type(static_cast<daObjCarry_c*>(a)->getType());
    if (kind >= 0) {
        n->best = d;
        n->kind = kind;
    }
    return nullptr;
}

int nearby_prop(const cXyz& pos) {
    NearSearch n{pos, kCopyDistance, -1};
    fopAcIt_Judge(judge_carry, &n);
    return n.kind;
}

void taunt(uint8_t sound) {
    const uint64_t now = now_ms();
    s_lastTaunt = now;
    match::send_taunt(sound);
}

// Warp everyone to the round's map, then tell the host once we're standing in it.
void follow_round(daAlink_c* l) {
    const match::Match& m = match::get();
    if (m.round != s_round) {
        s_round = m.round;
        s_warpPending = match::in_round();
        s_readyRound = 0;
        s_arrivedAt = 0;
    }
    if (!match::in_round()) {
        s_warpPending = false;
        return;
    }
    const MapInfo& map = round_map();
    const uint64_t now = now_ms();
    const bool busy = dComIfGp_isEnableNextStage() || dComIfGp_event_runCheck();
    if (s_warpPending && l != nullptr && (!busy || now - s_warpedAt > kWarpRetryMs)) {
        mods::log::info("warping to {} ({} room {} point {})", map.name, map.stage, map.room, map.point);
        game_mode::prepare_stage();
        arena::warp(map);
        s_warpPending = false;
        s_warpedAt = now;
        s_arrivedAt = 0;
        return;
    }
    const bool there = l != nullptr && std::strncmp(s_stage, map.stage, 8) == 0 && !busy;
    if (there && s_arrivedAt == 0 && now - s_warpedAt > 500) s_arrivedAt = now;
    if (!there) {
        s_arrivedAt = 0;
        // A loading zone must not let a hider escape into a different stage. Once this client has
        // reported ready for the round, bring it straight back after any accidental stage exit.
        if (l != nullptr && !busy && s_readyRound == m.round) s_warpPending = true;
    }
    if (s_arrivedAt != 0 && now - s_arrivedAt > kSettleMs && s_readyRound != m.round) {
        s_readyRound = m.round;
        match::report_ready();
    }
}

void on_phase_change(Phase from, Phase to) {
    s_swinging = false;
    s_swingHit = false;
    s_lastCutType = 0;
    s_attackReadyAt = 0;
    s_confirmedFinds = 0;
    if (to != Phase::Seek) {
        for (TauntPing& ping : s_tauntPings) ping = TauntPing{};
    }
    if (to == Phase::Gather && playing_prop_hunt()) {
        // Choose during the gathering/warp phase so the replacement model is already loaded when
        // the hider becomes disguised at the start of Hide.
        s_prop = random_prop(match::get().map);
        s_lastDecoy = 0;
    }
    if (to == Phase::Hide) {
        if (s_roundHealth) dComIfGs_setLife(kArenaLife);
        // A late join can first learn about a round after Gather has already ended.
        if (from != Phase::Gather && playing_prop_hunt()) s_prop = random_prop(match::get().map);
    }
    if (to == Phase::Seek) {
        s_lastTaunt = now_ms();
        s_lastTracking = 0;
        s_haveTracking = false;
    }
    (void)from;
}

void hunter_controls(daAlink_c* l) {
    const match::Match& m = match::get();
    const uint64_t now = now_ms();
    if (now - s_lastSwordCheck > 500) {
        s_lastSwordCheck = now;
        ensure_hunter_sword();
        // B normally refuses even to draw the sword in Castle Town. Keep it readied during the
        // hunt as a second line of defence in case that stage check was inlined by a game build.
        if (m.phase == Phase::Seek && l->mEquipItem != 0x103 && !l->checkEquipAnime()) {
            l->swordEquip(TRUE);
        }
    }
    // Backup for the setCutType hook, in case the game inlined that call: a new cut type is a
    // new swing.
    const u8 cut = l->getCutType();
    if (cut != 0 && cut != s_lastCutType && now >= s_attackReadyAt && (!s_swinging || now - s_swingAt > 100)) {
        s_swinging = true;
        s_swingHit = false;
        s_swingAt = now;
    }
    s_lastCutType = cut;
    if (s_swinging && now - s_swingAt > kSwingWindowMs) {
        s_swinging = false;
        if (!s_swingHit && m.settings.missPenalty && playing_prop_hunt() && m.phase == Phase::Seek) {
            // Current life is measured in quarters. Keep the final quarter to avoid a game over.
            const u16 life = dComIfGs_getLife();
            dComIfGs_setLife(life_after_miss(life));
            if (life <= 2) s_attackReadyAt = now + 2000;
            play_at(Z2SE_SY_CURSOR_CANCEL, &l->current.pos);
        }
    }
    if (m.phase != Phase::Seek) return;
    const uint8_t finds = match::player(net::self_id()).finds;
    if (finds > s_confirmedFinds) {
        dComIfGs_setLife(std::min<u16>(kArenaLife, dComIfGs_getLife() + 4));
        s_attackReadyAt = 0;
    }
    s_confirmedFinds = finds;
    if (m.settings.trackingPulse && mDoCPd_c::getTrigDown(PAD_1) &&
        (s_lastTracking == 0 || now - s_lastTracking >= kTrackingCooldownMs)) {
        float nearest = -1.0f;
        for (int id = 1; id <= kMaxPlayers; ++id) {
            const auto& p = match::player(id);
            if (!p.present || p.role != Role::Hider || p.found || !p.hasState ||
                !(p.state.flags & STATE_IN_WORLD) || now - p.stateAt > 2000 ||
                std::strncmp(p.state.stage, stage(), 8) != 0) continue;
            const cXyz at(p.state.x, p.state.y, p.state.z);
            const float distance = (at - l->current.pos).abs();
            if (nearest < 0.0f || distance < nearest) {
                nearest = distance;
                s_trackingPosition = at;
            }
        }
        if (nearest >= 0.0f) {
            s_lastTracking = now;
            s_haveTracking = true;
            play_at(Z2SE_SY_HINT_BUTTON_BLINK, nullptr);
        } else play_at(Z2SE_SY_CURSOR_CANCEL, nullptr);
    }
    if (playing_prop_hunt()) {
        if (now < s_attackReadyAt) return;
        // Link cannot draw a sword while swimming. In that one state B becomes a short-range tag,
        // using the same authoritative distance check as sword hits. It is deliberately not
        // consumed, so normal swimming controls continue to work.
        if (l->checkModeFlg(daAlink_c::MODE_SWIMMING) && mDoCPd_c::getTrigB(PAD_1)) {
            int nearest = 0;
            float nearestDistanceSq = 0.0f;
            for (int id = 1; id <= kMaxPlayers; ++id) {
                const match::Player& p = match::player(id);
                if (id == net::self_id() || !p.present || p.role != Role::Hider || p.found) continue;
                cXyz feet;
                float height;
                if (!puppet::anchor(id, feet, height)) continue;
                const int kind = p.state.prop < prop_count() ? p.state.prop : 0;
                const float reach = std::clamp(
                    prop_info(kind).radius + kSwimTagBodyRadius, 75.0f, 185.0f);
                const float dx = feet.x - l->current.pos.x;
                const float dz = feet.z - l->current.pos.z;
                const float distanceSq = dx * dx + dz * dz;
                const bool overlapsVertically =
                    l->current.pos.y >= feet.y - kSwimTagVerticalMargin &&
                    l->current.pos.y <= feet.y + height + kSwimTagVerticalMargin;
                if (overlapsVertically && distanceSq <= reach * reach &&
                    (nearest == 0 || distanceSq < nearestDistanceSq)) {
                    nearest = id;
                    nearestDistanceSq = distanceSq;
                }
            }
            if (nearest != 0) {
                note_hit();
                match::report_hit(static_cast<uint8_t>(nearest));
                play_at(Z2SE_SY_CURSOR_OK, &l->current.pos);
            }
        }
        return;
    }
    // Hide & Seek: touching a hider finds them.
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const match::Player& p = match::player(id);
        if (id == net::self_id() || !p.present || p.role != Role::Hider || p.found) continue;
        cXyz feet;
        float height;
        if (puppet::anchor(id, feet, height) && (feet - l->current.pos).abs() < kTouchDistance) {
            match::report_hit(static_cast<uint8_t>(id));
        }
    }
}

void hider_controls(daAlink_c* l) {
    const match::Match& m = match::get();
    const uint64_t now = now_ms();
    if (s_disguised) {
        if (mDoCPd_c::getTrigUp(PAD_1)) {
            if (match::can_place_decoy() && now - s_lastDecoy >= kDecoyCooldownMs) {
                s_lastDecoy = now;
                match::place_decoy();
                play_at(Z2SE_SY_CURSOR_OK, &l->current.pos);
            } else {
                play_at(Z2SE_SY_CURSOR_CANCEL, &l->current.pos);
            }
        } else if (mDoCPd_c::getTrigRight(PAD_1)) {
            const int near = nearby_prop(l->current.pos);
            s_prop = near >= 0 && near != s_prop ? near : step_prop(s_prop, m.map, 1);
            play_at(Z2SE_SY_CURSOR_OK, &l->current.pos);
        } else if (mDoCPd_c::getTrigLeft(PAD_1)) {
            s_prop = step_prop(s_prop, m.map, -1);
            play_at(Z2SE_SY_CURSOR_OK, &l->current.pos);
        }
    }
    if (m.phase != Phase::Seek) return;
    if (mDoCPd_c::getTrigDown(PAD_1) && now - s_lastTaunt > kTauntCooldownMs) {
        taunt(static_cast<uint8_t>(std::uniform_int_distribution<int>(0, kTauntCount - 1)(s_rng)));
    }

}

}  // namespace

bool init() {
    s_hooked = mods::hook::add_post<HsLinkExecute>(on_link_execute_post) == MOD_OK;
    s_hooked = mods::hook::add_pre<HsLinkDraw>(on_link_draw_pre) == MOD_OK && s_hooked;
    s_hooked = mods::hook::add_post<HsLinkDraw>(on_link_draw_post) == MOD_OK && s_hooked;
    if (mods::hook::add_post<HsLinkSetCutType>(on_set_cut_type_post) != MOD_OK) {
        mods::log::warn("sword swing hook unavailable: no miss penalty");
    }
    if (mods::hook::add_pre<HsLinkDamage>(on_link_damage_pre) != MOD_OK) {
        mods::log::warn("damage hook unavailable: world hazards can hurt players");
    }
    if (mods::hook::add_pre<HsCheckNotBattleStage>(on_not_battle_stage_pre) != MOD_OK) {
        mods::log::warn("battle-stage hook unavailable: swords may be blocked in Castle Town");
    }
    if (!s_hooked) mods::log::warn("Link hooks unavailable: hunters won't be held and props stay visible");
    match::set_hooks({.taunt = play_taunt, .roundStarted = nullptr, .foundMe = nullptr});
    return true;
}

void shutdown() {
    s_disguised = false;
    s_frozen = false;
    if (s_roundHealth && !game_mode::active()) {
        dComIfGs_setMaxLife(s_previousMaxLife);
        dComIfGs_setLife(s_previousLife);
    }
    s_roundHealth = false;
}

void update() {
    daAlink_c* l = link();
    const char* stage = dComIfGp_getStartStageName();
    s_inWorld = l != nullptr && stage != nullptr && stage[0] != '\0';
    copy_str(s_stage, s_inWorld ? stage : "");

    const bool online = net::status() == net::Status::Online;
    const match::Match& m = match::get();
    const Role role = match::my_role();
    const uint64_t now = now_ms();

    // Normal story saves borrow the arena's five-heart capacity for the round, then get their
    // original capacity/life back on leaving. The dedicated game mode always uses five hearts.
    if (online && s_inWorld && match::in_round() && !s_roundHealth) {
        s_previousMaxLife = static_cast<u8>(dComIfGs_getMaxLife());
        s_previousLife = dComIfGs_getLife();
        s_roundHealth = true;
        dComIfGs_setMaxLife(kArenaHeartPieces);
        dComIfGs_setLife(kArenaLife);
    } else if (s_roundHealth && (!online || m.phase == Phase::Lobby)) {
        if (!game_mode::active()) {
            dComIfGs_setMaxLife(s_previousMaxLife);
            dComIfGs_setLife(s_previousLife);
        }
        s_roundHealth = false;
    }

    if (m.phase != s_lastPhase) {
        on_phase_change(s_lastPhase, m.phase);
        s_lastPhase = m.phase;
    }

    if (online) follow_round(l);

    const bool onMap = s_inWorld && match::in_round() && std::strncmp(s_stage, round_map().stage, 8) == 0;
    const bool gathered = onMap && s_arrivedAt != 0;
    const bool freeze = online && s_inWorld &&
                        ((m.phase == Phase::Gather && gathered) ||
                            (m.phase == Phase::Hide && role == Role::Hunter && onMap));
    if (freeze && !s_frozen && l != nullptr) s_holdPos = l->current.pos;
    s_frozen = freeze;

    s_disguised = online && s_inWorld && playing_prop_hunt() && role == Role::Hider &&
                  !match::player(net::self_id()).found &&
                  (m.phase == Phase::Hide || m.phase == Phase::Seek) && l != nullptr && !l->checkWolf();

    if (l != nullptr && s_inWorld && !dComIfGp_event_runCheck()) {
        if (role == Role::Hider) hider_controls(l);
        if (role == Role::Hunter) hunter_controls(l);
    }

    if (online && match::in_round()) {
        // We read the triggers above, then consume every D-pad direction before Link's actor runs.
        // This keeps prop selection and taunts from also opening the vanilla Items/Map menus.
        interface_of_controller_pad& pad = mDoCPd_c::getCpadInfo(PAD_1);
        pad.mButtonFlags &= ~kRoundDpadMask;
        pad.mPressedButtonFlags &= ~kRoundDpadMask;
    }

    if (online && l != nullptr && s_inWorld) {
        PlayerState state;
        read_state(l, state);
        match::set_local_state(state);
    } else if (online) {
        PlayerState state;  // on a menu or loading: others hide our puppet
        state.flags = STATE_COMPACT;
        match::set_local_state(state);
    }

    if (now - s_lastColorCheck > 1000) {
        s_lastColorCheck = now;
        // Your own tunic changes colour only while playing: in a room, or in the Hide & Seek
        // game mode. Normal single-player keeps Link's green.
        uint8_t color = 0;
        if (online) color = match::player(net::self_id()).color;
        else if (game_mode::active()) color = settings::color();
        linkkit::recolor_local_link(color);
        // Load Link's files before the first other player shows up, not in the middle of play.
        if (online && s_inWorld) linkkit::ready();
    }
}

bool in_world() {
    return s_inWorld;
}

const char* stage() {
    return s_stage;
}

bool disguised() {
    return s_disguised;
}

int prop() {
    return s_prop;
}

bool blindfolded() {
    return s_frozen && match::get().phase == Phase::Hide && match::my_role() == Role::Hunter;
}

float taunt_cooldown() {
    const uint64_t since = now_ms() - s_lastTaunt;
    return since >= kTauntCooldownMs ? 0.0f : 1.0f - static_cast<float>(since) / kTauntCooldownMs;
}

bool has_sword() {
    return dComIfGs_getSelectEquipSword() != dItemNo_NONE_e;
}

uint32_t tracking_cooldown_secs() {
    if (s_lastTracking == 0 || now_ms() - s_lastTracking >= kTrackingCooldownMs) return 0;
    return static_cast<uint32_t>((kTrackingCooldownMs - (now_ms() - s_lastTracking) + 999) / 1000);
}

bool tracking_clue(cXyz& position) {
    if (!s_haveTracking || match::my_role() != Role::Hunter || match::get().phase != Phase::Seek ||
        !match::get().settings.trackingPulse || now_ms() - s_lastTracking >= kTrackingRevealMs) return false;
    position = s_trackingPosition;
    return true;
}

float taunt_ping(int id, cXyz& position) {
    if (id < 1 || id > kMaxPlayers || match::my_role() != Role::Hunter ||
        match::get().phase != Phase::Seek) {
        return 0.0f;
    }
    const TauntPing& ping = s_tauntPings[id];
    if (ping.at == 0) return 0.0f;
    const uint64_t age = now_ms() - ping.at;
    if (age >= kTauntRevealMs) return 0.0f;
    position = ping.position;
    return 1.0f - static_cast<float>(age) / static_cast<float>(kTauntRevealMs);
}

uint32_t attack_recovery_ms() {
    return s_attackReadyAt > now_ms() ? static_cast<uint32_t>(s_attackReadyAt - now_ms()) : 0;
}

ClueKind taunt_kind() { return s_myClueKind; }

void note_hit() {
    s_swingHit = true;
}

void play_taunt(uint8_t from, uint8_t sound, ClueKind kind) {
    if (from == net::self_id()) {
        s_lastTaunt = now_ms();
        s_myClueKind = kind;
        if (auto* l = link()) play_at(kTaunts[sound % kTauntCount], &l->current.pos);
        play_at(Z2SE_SY_HINT_BUTTON_BLINK, nullptr);
        return;
    }
    if (from < 1 || from > kMaxPlayers) return;
    cXyz feet;
    float height;
    if (!puppet::anchor(from, feet, height)) {
        const match::Player& p = match::player(from);
        if (!p.hasState || !(p.state.flags & STATE_IN_WORLD) ||
            std::strncmp(p.state.stage, stage(), 8) != 0) {
            return;
        }
        feet.set(p.state.x, p.state.y, p.state.z);
    }
    play_at(kTaunts[sound % kTauntCount], &feet);
    if (match::my_role() == Role::Hunter && match::get().phase == Phase::Seek) {
        s_tauntPings[from] = {feet, now_ms()};
        // The voice remains positional; this cue makes sure a distant taunt is not silently lost.
        play_at(Z2SE_SY_HINT_BUTTON_BLINK, nullptr);
    }
}

}  // namespace hs::local
