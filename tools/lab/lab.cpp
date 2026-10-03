// Opt-in in-game test harness (HS_LAB). Real relay, rounds, puppets and stage edits; the harness
// only hosts a local room, starts rounds with a chosen role, places decoy galleries and moves the
// camera so tools/lab/lab.py can take screenshots. Commands come from HS_LAB_CONTROL:
//   "serial action [arguments]"
#include "arena.hpp"
#include "common.hpp"
#include "local.hpp"
#include "match.hpp"
#include "maps.hpp"
#include "net.hpp"
#include "props.hpp"
#include "settings.hpp"
#include <mods/svc/hook.hpp>
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "d/actor/d_a_alink.h"
#include "d/d_bg_s_gnd_chk.h"
#include "m_Do/m_Do_controller_pad.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

DEFINE_HOOK(&mDoCPd_c::read, LabPad);
extern "C" void PADSetKeyboardActive(u32, BOOL);

namespace hs::lab {
namespace {
bool hooked = false;
uint64_t lastRead = 0;
long lastSerial = -1;
float sx = 0, sy = 0;
u32 button = 0;
int buttonTicks = 0;
int holdTicks = 0;
bool filmCamera = false;
float camAngle = 0, camDistance = 600, camHeight = 250, camFov = 55;
float centerX = 0, centerZ = 0;
cXyz anchor{0, 0, 0};

void pad_post(ModContext*, void*, void*, void*) {
    auto& pad = mDoCPd_c::getCpadInfo(0);
    pad.mMainStickPosX = sx;
    pad.mMainStickPosY = sy;
    pad.mMainStickValue = std::hypot(sx, sy);
    pad.mMainStickAngle = static_cast<s16>(std::atan2(sx, sy) * 32768.0 / M_PI);
    if (buttonTicks > 0) {
        pad.mButtonFlags |= button;
        if (buttonTicks == holdTicks) pad.mPressedButtonFlags |= button;
        --buttonTicks;
    }
}

float ground(float x, float y, float z) {
    cXyz probe(x, y + 300.0f, z);
    dBgS_LinkGndChk check;
    check.SetPos(&probe);
    const float h = dComIfG_Bgsp().GroundCross(&check);
    return h == -G_CM3D_F_INF ? y : h;
}

void start_round(int map, bool hunter, int hide, int seek) {
    if (match::in_round()) match::end_round();
    auto rules = match::get().settings;
    rules.map = static_cast<uint8_t>(map);
    rules.hideSecs = static_cast<uint16_t>(hide);
    rules.seekSecs = static_cast<uint16_t>(seek);
    rules.hunters = 1;
    rules.autoNext = false;
    rules.isPublic = false;
    // Choose roles through the normal rotation counter; everything afterwards is ordinary play.
    auto& players = const_cast<match::Match&>(match::get()).players;
    bool first = true;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (!players[id].present) continue;
        const bool wantsHunter = hunter ? id == net::self_id() : (id != net::self_id() && first);
        if (id != net::self_id() && !hunter) first = false;
        players[id].hunterRounds = wantsHunter ? 0 : 10;
    }
    match::set_settings(rules);
    match::start_round();
}

struct NativeEntry { cXyz pos; s16 name; s16 yaw; };
std::vector<NativeEntry> s_natives;
int collect(void* raw, void*) {
    auto* a = static_cast<fopAc_ac_c*>(raw);
    s_natives.push_back({a->current.pos, fopAcM_GetName(a), a->shape_angle.y});
    return 1;
}

void dump_actors(const std::string& path) {
    s_natives.clear();
    fopAcIt_Executor(collect, nullptr);
    std::ofstream out(path);
    for (const auto& n : s_natives) out << n.name << ' ' << n.pos.x << ' ' << n.pos.y << ' ' << n.pos.z << ' ' << n.yaw << '\n';
}
}  // namespace

void update() {
    const char* control = std::getenv("HS_LAB_CONTROL");
    if (!control) return;
    if (!hooked) {
        mods::hook::add_post<LabPad>(pad_post);
        PADSetKeyboardActive(0, TRUE);
        hooked = true;
    }
    if (!local::in_world() || dComIfGp_isEnableNextStage()) return;
    if (net::status() == net::Status::Offline) {
        net::host_room(settings::server(), "Lab");
        return;
    }
    if (net::status() != net::Status::Online) return;
    dComIfGs_setTime(180.0f);
    dComIfGp_roomControl_setTimePass(false);
    auto* link = daAlink_getAlinkActorClass();
    if (!link) return;
    const auto now = now_ms();
    if (now - lastRead > 150) {
        lastRead = now;
        std::ifstream file(control);
        long serial;
        std::string action;
        if (file >> serial >> action && serial != lastSerial) {
            lastSerial = serial;
            if (action == "round") {
                int map = 0, hide = 10, seek = 600; std::string role;
                file >> map >> role >> hide >> seek;
                start_round(map, role == "hunter", hide, seek);
            } else if (action == "end") {
                if (match::in_round()) match::end_round();
            } else if (action == "camera") {
                file >> camAngle >> camDistance >> camHeight >> camFov >> centerX >> centerZ;
                anchor = link->current.pos;
                filmCamera = true;
            } else if (action == "camat") {
                // Absolute look-at point plus eye offset.
                float lx, ly, lz;
                file >> lx >> ly >> lz >> camAngle >> camDistance >> camHeight >> camFov;
                anchor.set(lx, ly - 100, lz);
                centerX = centerZ = 0;
                filmCamera = true;
            } else if (action == "follow") {
                filmCamera = false;
                if (auto* cam = dCam_getBody()) cam->Start();
            } else if (action == "move") {
                file >> sx >> sy;
            } else if (action == "button") {
                int ticks = 2;
                file >> button >> ticks;
                buttonTicks = holdTicks = std::max(2, ticks);
            } else if (action == "prop") {
                int kind = 0; file >> kind;
                local::set_prop(kind);
            } else if (action == "position") {
                float x, y, z; int yaw = 0;
                if (file >> x >> y >> z >> yaw) {
                    if (y < -1e8f) y = ground(x, link->current.pos.y, z);
                    link->current.pos = link->old.pos = link->field_0x3798 = cXyz(x, y, z);
                    link->shape_angle.y = link->current.angle.y = static_cast<s16>(yaw);
                    link->speedF = 0;
                }
            } else if (action == "gallery") {
                // gallery <spacing> <id> <id> ...: a row of decoys to the right of Link.
                float spacing = 150; file >> spacing;
                match::lab_clear_decoys();
                int kind, index = 0;
                const float yaw = link->shape_angle.y * (M_PI / 32768.0);
                const float rx = std::cos(yaw), rz = -std::sin(yaw);
                const float fx = std::sin(yaw), fz = std::cos(yaw);
                while (file >> kind) {
                    const float x = link->current.pos.x + fx * 250 + rx * spacing * (index - 2);
                    const float z = link->current.pos.z + fz * 250 + rz * spacing * (index - 2);
                    match::lab_add_decoy(static_cast<uint8_t>(kind), x, ground(x, link->current.pos.y, z), z, link->shape_angle.y);
                    ++index;
                }
            } else if (action == "warp") {
                // A plain native warp, outside any round: stage, point, room.
                std::string stage; int point = 0, room = 0;
                file >> stage >> point >> room;
                if (match::in_round()) match::end_round();
                arena::warp_native(stage.c_str(), static_cast<int16_t>(point), static_cast<int8_t>(room));
            } else if (action == "clear") {
                match::lab_clear_decoys();
            } else if (action == "actors") {
                dump_actors(std::string(control) + ".actors");
            } else if (action == "taunt") {
                match::send_taunt(0);
            }
            mods::log::info("LAB command {}: {}", serial, action);
        }
        std::ofstream status(std::string(control) + ".status");
        const auto& m = match::get();
        status << net::room_code() << ' ' << static_cast<int>(m.phase) << ' ' << m.round << ' '
               << static_cast<int>(match::my_role()) << ' ' << link->current.pos.x << ' '
               << link->current.pos.y << ' ' << link->current.pos.z << ' ' << link->shape_angle.y
               << ' ' << local::stage() << ' ' << static_cast<int>(m.map) << ' '
               << arena::edge_distance(link->current.pos.x, link->current.pos.z) << ' '
               << net::member_count() << ' ' << local::prop() << ' ' << (local::disguised() ? 1 : 0) << '\n';
    }
    if (filmCamera) {
        if (auto* cam = dCam_getBody()) {
            const float a = camAngle * M_PI / 180;
            const cXyz center(anchor.x + centerX, anchor.y + 100, anchor.z + centerZ);
            const cXyz eye(center.x + std::sin(a) * camDistance, anchor.y + camHeight,
                           center.z + std::cos(a) * camDistance);
            cam->Stop();
            cam->SetTrimSize(0);
            cam->Set(center, eye, camFov, static_cast<s16>(0));
        }
    }
}
}  // namespace hs::lab
