#include "arena.hpp"
#include "arena_geometry.hpp"
#include "common.hpp"
#include "game_mode.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"

#include <mods/svc/hook.hpp>
#include <mods/svc/stage.h>
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_scene_exit2.h"
#include "d/d_attention.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_iter.h"
#include "SSystem/SComponent/c_math.h"

#include <array>
#include <cstring>
#include <vector>

DEFINE_HOOK(&daAlink_c::execute, ArenaLinkExecute);
DEFINE_HOOK(&daAlink_c::checkSceneChange, ArenaSceneCheck);
DEFINE_HOOK(&dStage_changeScene, ArenaSceneChange);
DEFINE_HOOK(&dStage_changeScene4Event, ArenaEventSceneChange);
DEFINE_HOOK(static_cast<void(*)(const char*, s16, s8, s8, f32, u32, int, s8, s16, int, int)>(&dComIfGp_setNextStage), ArenaNextStage);
DEFINE_HOOK(&dCamera_c::setMapToolData, ArenaCameraTools);
DEFINE_HOOK(&dCamera_c::nextType, ArenaCameraType);
DEFINE_HOOK(&dCamera_c::GetCameraTypeFromToolData, ArenaCameraToolType);
DEFINE_HOOK(&dAttention_c::Run, ArenaAttention);

namespace hs::arena {
namespace {
bool s_warpAllowed = false;
bool s_tracking = false;
bool s_haveSafe = false;
uint32_t s_round = 0;
Point s_before{};
cXyz s_safe{0.0f, 0.0f, 0.0f};
std::vector<ExitVolume> s_exits;
std::vector<StageActorHandle> s_edits;
int s_editedMap = -1;

struct HiddenAttention {
    fopAc_ac_c* actor;
    u32 flags;
};
std::vector<HiddenAttention> s_hiddenAttention;

int collect_exit(void* raw, void*) {
    auto* actor = static_cast<fopAc_ac_c*>(raw);
    const s16 name = fopAcM_GetName(actor);
    if (name == fpcNm_SCENE_EXIT_e) {
        s_exits.push_back({{actor->current.pos.x, actor->current.pos.y, actor->current.pos.z},
            actor->scale.x, actor->scale.y, actor->scale.z,
            cM_ssin(actor->shape_angle.y), cM_scos(actor->shape_angle.y)});
    } else if (name == fpcNm_SCENE_EXIT2_e) {
        auto* exit = static_cast<daScExit_c*>(actor);
        s_exits.push_back({{actor->current.pos.x, actor->current.pos.y, actor->current.pos.z},
            exit->mRadius, 0.0f, 0.0f, 0.0f, 1.0f, true});
    }
    return 1;
}

bool blocked(Point position) {
    for (const auto& exit : s_exits) if (overlaps_exit(exit, position)) return true;
    // Loading zones also live on the native floor polygons, without a scene-exit actor. Probe
    // Link's footprint, not just its centre, to make their edges behave like invisible walls.
    constexpr float offsets[][2] = {{0, 0}, {35, 0}, {-35, 0}, {0, 35}, {0, -35}};
    for (const auto& offset : offsets) {
        cXyz probe(position.x + offset[0], position.y + 80.0f, position.z + offset[1]);
        dBgS_LinkGndChk ground;
        ground.SetPos(&probe);
        const float y = dComIfG_Bgsp().GroundCross(&ground);
        if (y != -G_CM3D_F_INF && position.y - y < 600.0f &&
            dComIfG_Bgsp().GetExitId(ground) != 0x3F) return true;
    }
    return false;
}

const MapInfo& round_map() {
    return map_info(match::get().map);
}

HookAction link_pre(ModContext*, void* args, void*, void*) {
    s_tracking = locked() && !dComIfGp_isEnableNextStage();
    if (!s_tracking) { s_haveSafe = false; return HOOK_CONTINUE; }
    auto* link = mods::arg<daAlink_c*>(args, 0);
    if (s_round != match::get().round) { s_round = match::get().round; s_haveSafe = false; }
    s_before = {link->current.pos.x, link->current.pos.y, link->current.pos.z};
    s_exits.clear();
    fopAcIt_Executor(collect_exit, nullptr);
    return HOOK_CONTINUE;
}

void hold_back(daAlink_c* link, Point to) {
    link->current.pos.set(to.x, to.y, to.z);
    link->old.pos = link->current.pos;
    link->field_0x3798 = link->current.pos;
    link->speedF = 0.0f;
    link->speed.x = link->speed.z = 0.0f;
}

void link_post(ModContext*, void* args, void*, void*) {
    if (!s_tracking) return;
    auto* link = mods::arg<daAlink_c*>(args, 0);
    const Point target{link->current.pos.x, link->current.pos.y, link->current.pos.z};
    // Entrance spawn points can be inside their own loading strip. Let the player step out of
    // that strip, then enforce it. Scene-change hooks already prevent a load during this escape.
    if (!blocked(s_before)) {
        const Point allowed = sweep(s_before, target, blocked);
        if (allowed.x != target.x || allowed.y != target.y || allowed.z != target.z) hold_back(link, allowed);
    }
    // Void/lava/fog recovery remains within this loaded arena and preserves hunt health/time.
    if (s_haveSafe && (link->mGroundCode == 4 || link->mGroundCode == 9 || link->mGroundCode == 10)) {
        link->current.pos = link->old.pos = link->field_0x3798 = s_safe;
        link->speed.set(0.0f, 0.0f, 0.0f);
        link->speedF = 0.0f;
        link->procWaitInit();
    } else if (link->mLinkAcch.ChkGroundHit() && !blocked({link->current.pos.x, link->current.pos.y, link->current.pos.z})) {
        s_safe = link->current.pos;
        s_haveSafe = true;
    }
}

HookAction block_scene(ModContext*, void*, void* retval, void*) {
    if (!locked() || s_warpAllowed) return HOOK_CONTINUE;
    if (retval != nullptr) *static_cast<int*>(retval) = 0;
    return HOOK_SKIP_ORIGINAL;
}

HookAction block_next_stage(ModContext*, void*, void*, void*) {
    return locked() && !s_warpAllowed ? HOOK_SKIP_ORIGINAL : HOOK_CONTINUE;
}

HookAction camera_tools(ModContext*, void* args, void*, void*) {
    if (!game_mode::sandboxed()) return HOOK_CONTINUE;
    auto* camera = mods::arg<dCamera_c*>(args, 0);
    camera->mRoomMapTool.Clr();
    camera->mTagCamTool.Clr();
    camera->mStageCamTool.Clr();
    camera->mDefRoomCamTool.Clr();
    return HOOK_SKIP_ORIGINAL;
}

HookAction camera_type(ModContext*, void* args, void* retval, void*) {
    if (!game_mode::sandboxed()) return HOOK_CONTINUE;
    auto* camera = mods::arg<dCamera_c*>(args, 0);
    const int type = camera->GetCameraTypeFromCameraName("FieldS");
    if (type < 0 || type >= camera->mCamTypeNum) return HOOK_CONTINUE;
    if (retval != nullptr) *static_cast<int*>(retval) = type;
    return HOOK_SKIP_ORIGINAL;
}

// During a round nothing in the world answers the A button or Z-targeting: no "Lift" on a real
// pot, "Read" on a sign, "Call" on Horse Grass or "Speak" to a townsperson. A disguise cannot do
// any of that, so a real object must not either, or one button press would expose every prop.
int hide_attention(void* raw, void*) {
    auto* actor = static_cast<fopAc_ac_c*>(raw);
    if (actor == dComIfGp_getPlayer(0) || actor->attention_info.flags == 0) return 1;
    s_hiddenAttention.push_back({actor, actor->attention_info.flags});
    actor->attention_info.flags = 0;
    return 1;
}

HookAction attention_pre(ModContext*, void*, void*, void*) {
    s_hiddenAttention.clear();
    if (!locked() || !match::in_round()) return HOOK_CONTINUE;
    fopAcIt_Executor(hide_attention, nullptr);
    return HOOK_CONTINUE;
}

void attention_post(ModContext*, void*, void*, void*) {
    for (const auto& hidden : s_hiddenAttention) hidden.actor->attention_info.flags = hidden.flags;
    s_hiddenAttention.clear();
}

// Stage records are stored big-endian, exactly as on the disc.
void put_u16(uint8_t* out, uint16_t v) { out[0] = v >> 8; out[1] = v & 0xFF; }
void put_u32(uint8_t* out, uint32_t v) { put_u16(out, v >> 16); put_u16(out + 2, v & 0xFFFF); }
void put_f32(uint8_t* out, float v) { uint32_t bits; std::memcpy(&bits, &v, 4); put_u32(out, bits); }

std::array<uint8_t, 0x20> record(const char* name, uint32_t params, float x, float y, float z,
    int16_t ax, int16_t ay, int16_t az) {
    std::array<uint8_t, 0x20> out{};
    std::strncpy(reinterpret_cast<char*>(out.data()), name, 8);
    put_u32(&out[0x08], params);
    put_f32(&out[0x0C], x);
    put_f32(&out[0x10], y);
    put_f32(&out[0x14], z);
    put_u16(&out[0x18], static_cast<uint16_t>(ax));
    put_u16(&out[0x1A], static_cast<uint16_t>(ay));
    put_u16(&out[0x1C], static_cast<uint16_t>(az));
    put_u16(&out[0x1E], 0xFFFF);
    return out;
}

void clear_edits() {
    if (svc_stage != nullptr) {
        for (StageActorHandle handle : s_edits) svc_stage->remove_actor_edit(mod_ctx, handle);
    }
    s_edits.clear();
    s_editedMap = -1;
}

// Every client registers the same edits from the same generated table before the same warp, so
// all players load an identical arena: a central spawn, the extra scenery, no event triggers.
bool apply_edits(const MapInfo& map, int index) {
    clear_edits();
    if (svc_stage == nullptr) return false;
    bool ok = true;
    const auto add = [&](uint8_t room, const std::array<uint8_t, 0x20>& rec) {
        StageActorHandle handle = 0;
        if (svc_stage->add_actor(mod_ctx, map.stage, room, -1, rec.data(), rec.size(), &handle) == MOD_OK) {
            s_edits.push_back(handle);
            return true;
        }
        return false;
    };
    ok &= add(static_cast<uint8_t>(map.room), record("Link", map.spawnParams, map.spawnX, map.spawnY,
        map.spawnZ, 0, map.spawnYaw, map.point));
    for (int i = 0; i < map.sceneryCount; ++i) {
        const Scenery& s = map.scenery[i];
        add(s.room, record(s.name, s.params, s.x, s.y, s.z, s.angleX, s.angleY, s.angleZ));
    }
    for (int i = 0; i < map.removedCount; ++i) {
        StageActorHandle handle = 0;
        if (svc_stage->delete_actor(mod_ctx, map.stage, 0xFF, -1, map.removed[i], &handle) == MOD_OK) {
            s_edits.push_back(handle);
        }
    }
    s_editedMap = index;
    if (!ok) mods::log::warn("arena: could not add the {} spawn; using its native start", map.name);
    return ok;
}
}  // namespace

bool locked() {
    return net::status() == net::Status::Online && match::get().phase != Phase::Lobby &&
           local::in_world() && std::strncmp(local::stage(), round_map().stage, 8) == 0;
}

void warp(int index) {
    const MapInfo& map = map_info(index);
    const bool custom = apply_edits(map, index);
    s_warpAllowed = true;
    dComIfGp_setNextStage(map.stage, custom ? map.point : map.fallbackPoint, map.room, map.layer);
    s_warpAllowed = false;
    s_haveSafe = false;
}

void warp_native(const char* stage, int16_t point, int8_t room) {
    clear_edits();
    s_warpAllowed = true;
    dComIfGp_setNextStage(stage, point, room, -1);
    s_warpAllowed = false;
    s_haveSafe = false;
}

void update() {
    // Edits live only for a round (and its results). Story play and the lobby are untouched.
    const auto phase = match::get().phase;
    if (s_editedMap >= 0 && (net::status() != net::Status::Online || phase == Phase::Lobby)) clear_edits();
}

void shutdown() {
    clear_edits();
}

bool overlaps_loading_exit(Point position, float radius) {
    for (const auto& exit : s_exits) if (overlaps_exit(exit, position, radius)) return true;
    return false;
}

void init() {
    bool ok = true;
    ok &= mods::hook::add_pre<ArenaLinkExecute>(link_pre) == MOD_OK;
    ok &= mods::hook::add_post<ArenaLinkExecute>(link_post) == MOD_OK;
    ok &= mods::hook::add_pre<ArenaSceneCheck>(block_scene) == MOD_OK;
    ok &= mods::hook::add_pre<ArenaSceneChange>(block_scene) == MOD_OK;
    ok &= mods::hook::add_pre<ArenaEventSceneChange>(block_scene) == MOD_OK;
    ok &= mods::hook::add_pre<ArenaNextStage>(block_next_stage) == MOD_OK;
    ok &= mods::hook::add_pre<ArenaCameraTools>(camera_tools) == MOD_OK;
    ok &= mods::hook::add_pre<ArenaCameraType>(camera_type) == MOD_OK;
    ok &= mods::hook::add_pre<ArenaCameraToolType>(camera_type) == MOD_OK;
    if (!ok) mods::log::warn("arena boundary/camera hook unavailable; check Dusklight version");
    if (mods::hook::add_pre<ArenaAttention>(attention_pre) != MOD_OK ||
        mods::hook::add_post<ArenaAttention>(attention_post) != MOD_OK) {
        mods::log::warn("attention hook unavailable: real objects can still be lifted and read");
    }
    if (svc_stage == nullptr) mods::log::warn("no stage service: arenas use native spawns without extra scenery");
}
}  // namespace hs::arena
