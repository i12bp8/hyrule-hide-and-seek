#include "local.hpp"

#include "common.hpp"
#include "game_mode.hpp"
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

namespace hs::local {

namespace {

constexpr uint64_t kTauntCooldownMs = 5000;
constexpr uint64_t kAutoTauntEveryMs = 20000;
constexpr uint32_t kAutoTauntLastMs = 60000;
constexpr uint64_t kSwingWindowMs = 650;
constexpr uint64_t kSettleMs = 1500;
constexpr uint64_t kWarpRetryMs = 12000;
constexpr float kTouchDistance = 90.0f;
constexpr float kCopyDistance = 300.0f;

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
uint64_t s_lastAutoTaunt = 0;

// Hunters
bool s_frozen = false;
cXyz s_holdPos{0.0f, 0.0f, 0.0f};
bool s_swinging = false;
bool s_swingHit = false;
uint64_t s_swingAt = 0;
u8 s_lastCutType = 0;
uint64_t s_lastColorCheck = 0;

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
    if (!s_disguised) return HOOK_CONTINUE;
    if (retval != nullptr) *static_cast<int*>(retval) = 1;
    return HOOK_SKIP_ORIGINAL;
}

// Link draws every frame we're in the world, which is when the 2D lists are being filled.
void on_link_draw_post(ModContext*, void*, void*, void*) {
    hud::queue();
}

void on_set_cut_type_post(ModContext*, void* args, void*, void*) {
    const u8 type = mods::arg<u8>(args, 1);
    if (type == 0) return;
    // A new swing: settle the previous one first.
    s_swinging = true;
    s_swingHit = false;
    s_swingAt = now_ms();
}

// ---- helpers ---------------------------------------------------------------------------------

void play_at(uint32_t sound, const cXyz* pos) {
    mDoAud_seStart(sound, pos, 0, 0);
}

void read_state(daAlink_c* l, PlayerState& s) {
    s.flags = STATE_IN_WORLD;
    if (l->checkWolf()) s.flags |= STATE_WOLF;
    if (s_disguised) s.flags |= STATE_DISGUISED;
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
    s_lastTaunt = now_ms();
    if (daAlink_c* l = link()) play_at(kTaunts[sound % kTauntCount], &l->current.pos);
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
        dComIfGp_setNextStage(map.stage, map.point, map.room, -1);
        s_warpPending = false;
        s_warpedAt = now;
        s_arrivedAt = 0;
        return;
    }
    const bool there = l != nullptr && std::strncmp(s_stage, map.stage, 8) == 0 && !busy;
    if (there && s_arrivedAt == 0 && now - s_warpedAt > 500) s_arrivedAt = now;
    if (!there) s_arrivedAt = 0;
    if (s_arrivedAt != 0 && now - s_arrivedAt > kSettleMs && s_readyRound != m.round) {
        s_readyRound = m.round;
        match::report_ready();
    }
}

void on_phase_change(Phase from, Phase to) {
    if (to == Phase::Hide) {
        // Everyone starts the round with full hearts.
        dComIfGs_setLife(static_cast<u16>(dComIfGs_getMaxLifeGauge()));
        s_prop = random_prop();
        s_lastAutoTaunt = now_ms();
    }
    (void)from;
}

void hunter_controls(daAlink_c* l) {
    const match::Match& m = match::get();
    const uint64_t now = now_ms();
    // Backup for the setCutType hook, in case the game inlined that call: a new cut type is a
    // new swing.
    const u8 cut = l->getCutType();
    if (cut != 0 && cut != s_lastCutType && (!s_swinging || now - s_swingAt > 100)) {
        s_swinging = true;
        s_swingHit = false;
        s_swingAt = now;
    }
    s_lastCutType = cut;
    if (s_swinging && now - s_swingAt > kSwingWindowMs) {
        s_swinging = false;
        if (!s_swingHit && m.settings.missPenalty && playing_prop_hunt() && m.phase == Phase::Seek) {
            // A swing at nothing costs a quarter heart, never the last one.
            const u16 life = dComIfGs_getLife();
            if (life > 1) dComIfGs_setLife(static_cast<u16>(life - 1));
            play_at(Z2SE_SY_CURSOR_CANCEL, &l->current.pos);
        }
    }
    if (m.phase != Phase::Seek || playing_prop_hunt()) return;
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
        if (mDoCPd_c::getTrigRight(PAD_1)) {
            const int near = nearby_prop(l->current.pos);
            s_prop = near >= 0 && near != s_prop ? near : (s_prop + 1) % prop_count();
            play_at(Z2SE_SY_CURSOR_OK, &l->current.pos);
        } else if (mDoCPd_c::getTrigLeft(PAD_1)) {
            s_prop = (s_prop + prop_count() - 1) % prop_count();
            play_at(Z2SE_SY_CURSOR_OK, &l->current.pos);
        }
    }
    if (m.phase != Phase::Seek) return;
    if (mDoCPd_c::getTrigDown(PAD_1) && now - s_lastTaunt > kTauntCooldownMs) {
        taunt(static_cast<uint8_t>(std::uniform_int_distribution<int>(0, kTauntCount - 1)(s_rng)));
    }
    if (m.settings.autoTaunt && match::ms_left() < kAutoTauntLastMs && now - s_lastAutoTaunt > kAutoTauntEveryMs) {
        s_lastAutoTaunt = now;
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
    if (!s_hooked) mods::log::warn("Link hooks unavailable: hunters won't be held and props stay visible");
    match::set_hooks({.taunt = play_taunt, .roundStarted = nullptr, .foundMe = nullptr});
    return true;
}

void shutdown() {
    s_disguised = false;
    s_frozen = false;
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

    if (online && l != nullptr && s_inWorld) {
        PlayerState state;
        read_state(l, state);
        match::set_local_state(state);
    } else if (online) {
        PlayerState state;  // on a menu or loading: others hide our puppet
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

void note_hit() {
    s_swingHit = true;
}

void play_taunt(uint8_t from, uint8_t sound) {
    cXyz feet;
    float height;
    if (puppet::anchor(from, feet, height)) play_at(kTaunts[sound % kTauntCount], &feet);
}

}  // namespace hs::local
