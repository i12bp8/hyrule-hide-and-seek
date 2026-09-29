#include "arena.hpp"
#include "arena_geometry.hpp"
#include "common.hpp"
#include "game_mode.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"

#include <mods/svc/hook.hpp>
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_scene_exit2.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_iter.h"
#include "SSystem/SComponent/c_math.h"

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

namespace hs::arena {
namespace {
bool s_warpAllowed = false;
bool s_tracking = false;
bool s_haveSafe = false;
uint32_t s_round = 0;
Point s_before{};
cXyz s_safe{0.0f, 0.0f, 0.0f};
std::vector<ExitVolume> s_exits;

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

void link_post(ModContext*, void* args, void*, void*) {
    if (!s_tracking) return;
    auto* link = mods::arg<daAlink_c*>(args, 0);
    const Point target{link->current.pos.x, link->current.pos.y, link->current.pos.z};
    // Entrance spawn points can be inside their own loading strip. Let the player step out of
    // that strip, then enforce it. Scene-change hooks already prevent a load during this escape.
    if (!blocked(s_before)) {
        const Point allowed = sweep(s_before, target, blocked);
        if (allowed.x != target.x || allowed.y != target.y || allowed.z != target.z) {
            link->current.pos.set(allowed.x, allowed.y, allowed.z);
            link->old.pos = link->current.pos;
            link->field_0x3798 = link->current.pos;
            link->speedF = 0.0f;
            link->speed.x = link->speed.z = 0.0f;
        }
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
}  // namespace

bool locked() {
    return net::status() == net::Status::Online && match::get().phase != Phase::Lobby &&
           local::in_world() && std::strncmp(local::stage(), map_info(match::get().map).stage, 8) == 0;
}

void warp(const MapInfo& map) {
    s_warpAllowed = true;
    dComIfGp_setNextStage(map.stage, map.point, map.room, -1);
    s_warpAllowed = false;
    s_haveSafe = false;
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
}
}  // namespace hs::arena
