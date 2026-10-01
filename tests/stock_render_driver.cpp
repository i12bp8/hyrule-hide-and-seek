// Run only in an isolated --user-dir on stock Dusklight, with --stage F_SP103,0,13,-1.
// This drives the real actors/render lists, not a mock GPU. Dusklight 2.0.3 supports WebSockets.
#include "common.hpp"
#include "arena.hpp"
#include "gameplay.hpp"
#include "game_mode.hpp"
#include "linkkit.hpp"
#include "maps.hpp"
#include "local.hpp"
#include "match.hpp"
#include "net.hpp"
#include "props.hpp"
#include "settings.hpp"
#include "ui.hpp"

#include <mods/svc/hook.hpp>
#include "JSystem/J3DGraphBase/J3DDrawBuffer.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "d/d_com_inf_game.h"
#include "d/d_camera.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_scene_exit.h"
#include "f_op/f_op_actor_iter.h"
#include "JSystem/J3DGraphAnimator/J3DAnimation.h"
#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_math.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <unordered_set>

extern const HookService* svc_hook;

namespace hs::testing {
namespace {
DEFINE_HOOK(&J3DDrawBuffer::drawHead, DrawHead);
DEFINE_HOOK(&J3DMatPacket::draw, MatDraw);
uint64_t s_start = 0;
uint64_t s_joined = 0;
bool s_round = false;
int s_kind = -1;
int s_checks = 0;
bool s_left = false;
const bool s_hunterOnly = std::getenv("HS_STOCK_HUNTER_TEST") != nullptr;
const bool s_arenaTest = std::getenv("HS_ARENA_TEST") != nullptr;
const bool s_uiTest = std::getenv("HS_UI_TEST") != nullptr;
const char* s_hudTest = std::getenv("HS_HUD_TEST");

void require(bool condition, const char* message) {
    if (condition) return;
    mods::log::error("STOCK_RENDER_TEST FAIL: {}", message);
    std::fflush(nullptr);
    std::_Exit(2);
}

void check_animations() {
    static bool checked = false;
    if (checked) return;
    checked = true;
    require(linkkit::ready(), "Link resources unavailable");
    for (const auto idx : {linkkit::kIdleAnim, linkkit::kWalkAnim, uint16_t(0x7F)}) {
        auto* animation = linkkit::anim(idx);
        require(animation != nullptr && animation->getKind() == 8, "valid BCK rejected (T-pose regression)");
        require(animation->getFrameMax() > 1, "animation has no frames");
        J3DAnmTransformKey first = *static_cast<J3DAnmTransformKey*>(animation);
        J3DAnmTransformKey second = first;
        first.setFrame(0);
        second.setFrame(animation->getFrameMax() * 0.25f);
        int changed = 0;
        for (int joint = 0; joint < 24; ++joint) {
            J3DTransformInfo a, b;
            first.getTransform(joint, &a);
            second.getTransform(joint, &b);
            require(std::isfinite(a.mTranslate.x) && std::isfinite(b.mScale.x), "invalid animation transform");
            changed += a.mRotation.x != b.mRotation.x || a.mRotation.y != b.mRotation.y ||
                       a.mRotation.z != b.mRotation.z;
        }
        require(changed > 0, "animation is a static bind pose");
    }
    mods::log::info("STOCK_RENDER_TEST: idle/walk/sword BCKs animate; frame copies independent");
}

HookAction check_packets(ModContext*, void* args, void*, void*) {
    const auto* buffer = mods::arg<const J3DDrawBuffer*>(args, 0);
    std::unordered_set<const J3DPacket*> materials;
    for (u32 bucket = 0; bucket < buffer->mEntryTableSize; ++bucket) {
        for (auto* p = buffer->mpBuffer[bucket]; p != nullptr; p = p->getNextPacket()) {
            if (!materials.insert(p).second) {
                mods::log::error("STOCK_RENDER_TEST FAIL: duplicate/cyclic material packet");
                std::exit(2);
            }
        }
    }
    ++s_checks;
    return HOOK_CONTINUE;
}

HookAction check_shapes(ModContext*, void* args, void*, void*) {
    // Immediate draw buffers also contain non-material packets; inspect shapes only when
    // an actual material packet is about to draw, rather than casting every buffer entry.
    const auto* material = mods::arg<const J3DMatPacket*>(args, 0);
    std::unordered_set<const J3DPacket*> shapes;
    for (auto* shape = material->getShapePacket(); shape != nullptr;
         shape = static_cast<J3DShapePacket*>(shape->getNextPacket())) {
        if (!shapes.insert(shape).second) {
            mods::log::error("STOCK_RENDER_TEST FAIL: cyclic shape packet");
            std::exit(2);
        }
    }
    return HOOK_CONTINUE;
}

void apply(uint8_t from, const Writer& w) {
    match::on_message(from, w.bytes().data(), w.bytes().size());
}

int s_arenaMap = -1;
uint64_t s_arenaLoaded = 0;
bool s_exitMoved = false;
bool s_exitChecked = false;
cXyz s_exitFrom{0.0f, 0.0f, 0.0f};
cXyz s_exitTo{0.0f, 0.0f, 0.0f};

int find_exit(void* raw, void*) {
    auto* actor = static_cast<fopAc_ac_c*>(raw);
    if (s_exitMoved || fopAcM_GetName(actor) != fpcNm_SCENE_EXIT_e) return 1;
    auto* player = daAlink_getAlinkActorClass();
    // Test the actual execute hook by making Link's previous position just outside an authored
    // exit volume and moving him to its centre immediately before the simulation actor pass.
    const float x = actor->scale.x + 80.0f;
    s_exitFrom = actor->current.pos + cXyz(cM_scos(actor->shape_angle.y) * x, 0,
                                         -cM_ssin(actor->shape_angle.y) * x);
    s_exitTo = actor->current.pos;
    player->current.pos = player->old.pos = player->field_0x3798 = s_exitFrom;
    s_exitMoved = true;
    return 1;
}

// Runs after arena's pre-hook captured the start position and before Link executes. The post-hook
// must stop this forced movement at the authored exit instead of loading/respawning the stage.
DEFINE_HOOK(&daAlink_c::execute, TestExitMotion);
HookAction move_toward_exit(ModContext*, void* args, void*, void*) {
    if (s_arenaTest && s_exitMoved && !s_exitChecked) {
        auto* player = mods::arg<daAlink_c*>(args, 0);
        player->current.pos = s_exitTo;
    }
    return HOOK_CONTINUE;
}

void arena_test_update(uint64_t now) {
    if (net::status() == net::Status::Offline) {
        net::host_room(settings::server(), "Arena regression");
        return;
    }
    if (net::status() != net::Status::Online || !local::in_world()) return;
    if (s_arenaMap == -1) {
        match::on_joined(2);
        require(mods::hook::add_pre<TestExitMotion>(move_toward_exit) == MOD_OK, "test motion hook unavailable");
    }
    if (s_arenaMap == -1 || s_arenaLoaded == UINT64_MAX) {
        ++s_arenaMap;
        if (s_arenaMap == map_count()) {
            mods::log::info("STOCK_RENDER_TEST PASS: all 15 maps, camera, exits, five hearts, animation; {} draw checks", s_checks);
            std::fflush(nullptr);
            std::_Exit(0);
        }
        match::end_round();
        match::Settings rules;
        rules.map = s_arenaMap;
        rules.hideSecs = 10;
        rules.seekSecs = 120;
        rules.autoNext = false;
        match::set_settings(rules);
        match::start_round();
        s_arenaLoaded = 0;
        s_exitMoved = s_exitChecked = false;
        return;
    }
    if (dComIfGp_isEnableNextStage() || std::strncmp(local::stage(), map_info(s_arenaMap).stage, 8) != 0) {
        s_arenaLoaded = 0;
        return;
    }
    if (s_arenaLoaded == 0) s_arenaLoaded = now;
    auto* player = daAlink_getAlinkActorClass();
    PlayerState remote;
    remote.flags = STATE_IN_WORLD | STATE_SWORD;
    copy_str(remote.stage, local::stage());
    remote.room = fopAcM_GetRoomNo(player);
    remote.x = player->current.pos.x + cM_ssin(player->shape_angle.y) * 350.0f;
    remote.y = player->current.pos.y;
    remote.z = player->current.pos.z + cM_scos(player->shape_angle.y) * 350.0f;
    remote.under[0] = {linkkit::kWalkAnim, static_cast<float>((now / 33) % 20), 255};
    remote.upper[0] = {0x7F, static_cast<float>((now / 33) % 20), 255};
    Writer state(MSG_STATE); remote.write(state); apply(2, state);
    Writer ready(MSG_READY); ready.u32(match::get().round); apply(2, ready);
    if (now - s_arenaLoaded < 4000) return;
    if (match::get().phase != Phase::Seek) {
        Writer phase(MSG_PHASE);
        phase.u32(match::get().round); phase.u8(static_cast<uint8_t>(Phase::Seek)); phase.u32(120000);
        apply(net::self_id(), phase);
        return;
    }
    require(!dComIfGp_event_runCheck(), "scripted event started in arena");
    auto* camera = dCam_getBody();
    require(camera != nullptr && camera->mCurType == camera->GetCameraTypeFromCameraName("FieldS"), "arena camera is not free FieldS camera");
    require(dComIfGs_getMaxLife() == kArenaHeartPieces && dComIfGs_getLife() <= kArenaLife, "arena exceeds five hearts");
    const int originalType = camera->mCurType;
    camera->SetTagData(player, 0, 0, 0);
    require(camera->nextType(originalType) == originalType, "camera tag overrode free camera");
    require(dStage_changeScene(0, 0.0f, 0, fopAcM_GetRoomNo(player), 0, -1) == 0, "stage exit was allowed");
    dComIfGp_setNextStage("F_SP103", 13, 0, -1);
    require(!dComIfGp_isEnableNextStage(), "unexpected map loading started");
    require(!dComIfGp_event_compulsory(player, nullptr, -1), "compulsory cutscene was allowed");
    if (!s_exitMoved) {
        fopAcIt_Executor(find_exit, nullptr);
        if (s_exitMoved) return;
    } else if (!s_exitChecked) {
        require((player->current.pos - s_exitTo).abs() > 35.0f, "exit collision allowed crossing into loading zone");
        s_exitChecked = true;
    }
    mods::log::info("STOCK_RENDER_TEST: {} camera/events/exits/health PASS (volume collision {})", map_info(s_arenaMap).name, s_exitChecked ? "checked" : "no actor exit in loaded rooms");
    s_arenaLoaded = UINT64_MAX;
}

void roster() {
    Writer w(MSG_ROSTER);
    w.u8(kMaxPlayers);
    for (int id = 1; id <= kMaxPlayers; ++id) {
        w.u8(id);
        w.u8(static_cast<uint8_t>(id == 2 ? Role::Hunter : Role::Hider));
        w.u8(id - 1);
        w.u16(0); w.u16(0);
        w.u8(id == 2 ? 6 : 10); w.u8(0); w.u8(0); w.u8(0); w.u8(0); w.u8(0); w.u8(0);
        w.u8(id == 2 ? kArenaLife : 0); w.u16(0);
    }
    apply(net::self_id(), w);
}

void decoys(int kind, const cXyz& at, int count = match::kMaxActiveDecoys) {
    Writer w(MSG_DECOYS);
    w.u32(1);
    w.u8(count);
    for (int i = 0; i < count; ++i) {
        w.u8(i + 1); w.u8(i / match::kMaxDecoysPerPlayer + 1); w.u8(kind);
        w.f32(at.x + (i % 16 - 8) * 120.0f);
        w.f32(at.y);
        w.f32(at.z + (i / 16 - 5) * 120.0f);
        w.s16(0);
    }
    apply(net::self_id(), w);
    if (match::decoy_count() != count) {
        mods::log::error("STOCK_RENDER_TEST FAIL: snapshot rejected");
        std::exit(2);
    }
    mods::log::info("STOCK_RENDER_TEST: {} decoys of {}", count, prop_info(kind).name);
}

void rupees(const cXyz& at) {
    Writer w(MSG_RUPEES);
    w.u32(1); w.u8(match::kMaxRupees);
    for (int i = 0; i < match::kMaxRupees; ++i) {
        w.u16(i + 1); w.f32(at.x + (i - 4) * 180.0f);
        w.f32(at.y); w.f32(at.z + 500.0f); w.u32(match::kRupeeLifetimeMs);
    }
    apply(net::self_id(), w);
    require(match::get().rupeeCount == match::kMaxRupees, "treasure snapshot rejected");
}
}  // namespace

void stock_render_update() {
    const auto now = now_ms();
    if (s_start == 0) {
        s_start = now;
        if (mods::hook::add_pre<DrawHead>(check_packets) != MOD_OK) std::exit(2);
        if (mods::hook::add_pre<MatDraw>(check_shapes) != MOD_OK) std::exit(2);
    }
    if (s_hudTest && s_joined != 0 && now - s_joined > 60000) {
        mods::log::info("HUD_INSPECTION: finished; {} draw-list checks", s_checks);
        std::fflush(nullptr);
        std::_Exit(0);
    }
    if (now - s_start > (s_arenaTest ? 600000u : 240000u)) {
        mods::log::error("STOCK_RENDER_TEST FAIL: timed out");
        std::exit(2);
    }
    if (!local::in_world() || dComIfGp_isEnableNextStage()) return;
    check_animations();
    if (s_arenaTest) {
        arena_test_update(now);
        return;
    }
    if (net::status() == net::Status::Offline && !s_left) {
        net::host_room(settings::server(), "Stock render test");
        return;
    }
    if (s_left) {
        if (now - s_joined > 2000) {
            mods::log::info("STOCK_RENDER_TEST PASS: lobby, all selectable prop kinds, 160 decoys, 8 rupees, cleanup; {} draw-list checks", s_checks);
            // This is an in-frame test, not application shutdown: exit() runs game globals'
            // destructors while the engine is still active. Gameplay actor cleanup ran above.
            std::fflush(nullptr);
            std::_Exit(0);
        }
        return;
    }
    if (net::status() != net::Status::Online) return;
    if (s_uiTest) {
        if (s_joined == 0) { s_joined = now; ui::open(); }
        if (now - s_joined > 120000) std::_Exit(0);
        return;
    }
    if (s_joined == 0) {
        s_joined = now;
        for (int id = 2; id <= (s_hudTest ? 4 : kMaxPlayers); ++id) match::on_joined(id);
        mods::log::info("STOCK_RENDER_TEST: full 16-player lobby");
    }
    const auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr) return;
    const cXyz at = link->current.pos;
    for (int id = 2; id <= (s_hudTest ? 4 : kMaxPlayers); ++id) {
        PlayerState state;
        state.flags = STATE_IN_WORLD;
        if (s_round && id != 2) state.flags |= STATE_DISGUISED;
        copy_str(state.stage, local::stage());
        state.room = 0;
        state.x = at.x + (id % 4 - 2) * 130.0f;
        state.y = at.y;
        state.z = at.z + (id / 4 + 1) * 130.0f;
        if (id == 2) {
            state.x = at.x + cM_ssin(link->shape_angle.y) * 350.0f;
            state.z = at.z + cM_scos(link->shape_angle.y) * 350.0f;
        }
        state.prop = s_kind >= 0 ? s_kind : 0;
        Writer w(MSG_STATE);
        state.write(w);
        apply(id, w);
    }
    if (!s_round && now - s_joined > 8000) {
        roster();
        Writer round(MSG_ROUND);
        round.u32(1); round.u8(0); round.u8(0); round.u16(600); round.u16(600);
        round.u8(kMaxPlayers - 1); round.u8(0);
        apply(net::self_id(), round);
        Writer phase(MSG_PHASE);
        phase.u32(1); phase.u8(static_cast<uint8_t>(Phase::Seek)); phase.u32(600000);
        apply(net::self_id(), phase);
        s_round = true;
    }
    if (!s_round || now - s_joined < 12000) return;
    if (s_hudTest && std::strcmp(s_hudTest, "reveal") == 0) {
        static uint64_t lastReveal = 0;
        if (now - lastReveal > 5000) {
            lastReveal = now;
            Writer w(MSG_CLUE); w.u32(1); w.u8(net::self_id()); w.u8(1);
            w.u8(static_cast<uint8_t>(ClueKind::Manual)); apply(net::self_id(), w);
        }
    }
    if (s_hunterOnly) {
        static bool hunting = false;
        if (!hunting) {
            Writer phase(MSG_PHASE);
            phase.u32(1); phase.u8(static_cast<uint8_t>(Phase::Seek)); phase.u32(600000);
            apply(net::self_id(), phase);
            hunting = true;
            mods::log::info("STOCK_RENDER_TEST: hunter visibility during Hunt (inspect viewport)");
            std::fflush(nullptr);
        }
        if (now - s_joined > 60000) {
            mods::log::info("STOCK_RENDER_TEST PASS: hunter scenario; {} draw-list checks", s_checks);
            std::fflush(nullptr);
            std::_Exit(0);
        }
        return;
    }
    static uint64_t changedAt = 0;
    if (now < changedAt || now - changedAt < 2000) return;
    changedAt = now;
    int next = s_kind + 1;
    while (next < prop_count() && !prop_on_map(next, -1)) ++next;
    if (next == prop_count()) {
        net::leave_room();
        match::on_disconnected();
        s_left = true;
        s_joined = now;
        return;
    }
    s_kind = next;
    decoys(s_kind, at, s_hudTest ? 3 : match::kMaxActiveDecoys);
    rupees(at);
    if (s_hudTest && std::strcmp(s_hudTest, "results") == 0) match::end_round();
    if (s_hudTest) {
        s_kind = -1;
        changedAt = now + 30000; // hold the layout long enough to inspect and capture it
    }
}
}  // namespace hs::testing
