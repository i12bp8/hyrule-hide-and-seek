// Opt-in local filming director. Real relay clients, actors, controls and round rules.
// Commands are read from HS_SHOWCASE_CONTROL: "serial action [arguments]".
#include "common.hpp"
#include "local.hpp"
#include "match.hpp"
#include "maps.hpp"
#include "net.hpp"
#include "settings.hpp"
#include <mods/svc/hook.hpp>
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "d/actor/d_a_alink.h"
#include "m_Do/m_Do_controller_pad.h"
#include "d/d_bg_s_gnd_chk.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cmath>

DEFINE_HOOK(&mDoCPd_c::read, ShowcasePad);
extern "C" void PADSetKeyboardActive(u32, BOOL);

namespace hs::showcase {
namespace {
bool hooked = false;
uint64_t lastRead = 0;
int lastSerial = -1;
float sx = 0, sy = 0;
u32 button = 0;
int buttonTicks = 0;
bool filmCamera = false;
bool orbit = false;
float angle = 0, distance = 950, height = 350, fov = 52;
float centerX = 0, centerZ = 0;
uint64_t viewStarted = 0;
cXyz anchor{0,0,0};

void pad_post(ModContext*, void*, void*, void*) {
    auto& pad = mDoCPd_c::getCpadInfo(0);
    pad.mMainStickPosX = sx;
    pad.mMainStickPosY = sy;
    pad.mMainStickValue = std::hypot(sx, sy);
    pad.mMainStickAngle = static_cast<s16>(std::atan2(sx, sy) * 32768.0 / M_PI);
    if (buttonTicks > 0) {
        pad.mButtonFlags |= button;
        if (buttonTicks == 2) pad.mPressedButtonFlags |= button;
        --buttonTicks;
    }
}

void scene(const std::string& name) {
    sx = sy = 0;
    if (match::in_round()) match::end_round();
    filmCamera = false;
    orbit = false;
    if (auto* cam = dCam_getBody()) cam->Start();
    if (name == "lobby") return;
    auto rules = match::get().settings;
    rules.map = name == "cats" ? 7 : name == "town" ? 5 : name == "kakariko" ? 2 : 0;
    rules.hideSecs = 8;
    rules.seekSecs = 180;
    rules.hunters = 1;
    rules.autoNext = false;
    rules.isPublic = false;
    const bool hostHunts = name == "hunt" || name == "town" || name == "kakariko";
    // Select roles before the normal host rules assign them. Everything afterwards uses the
    // same round, disguise, hit and scoring paths as ordinary play.
    auto& players = const_cast<match::Match&>(match::get()).players;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (!players[id].present) continue;
        players[id].hunterRounds = (hostHunts ? id == net::self_id() : id == 2) ? 0 : 10;
    }
    match::set_settings(rules);
    match::start_round();
}
}

void update() {
    const char* control = std::getenv("HS_SHOWCASE_CONTROL");
    if (!control) return;
    if (!hooked) {
        mods::hook::add_post<ShowcasePad>(pad_post);
        PADSetKeyboardActive(0, TRUE);
        hooked = true;
    }
    if (!local::in_world() || dComIfGp_isEnableNextStage()) return;
    if (net::status() == net::Status::Offline) {
        net::host_room(settings::server(), "i12bp8");
        return;
    }
    if (net::status() != net::Status::Online) return;
    dComIfGs_setTime(180.0f);
    dComIfGp_roomControl_setTimePass(false);
    auto* link = daAlink_getAlinkActorClass();
    if (!link) return;
    const auto now = now_ms();
    if (now - lastRead > 200) {
        lastRead = now;
        std::ifstream file(control);
        int serial;
        std::string action;
        if (file >> serial >> action && serial != lastSerial) {
            lastSerial = serial;
            if (action == "scene") {
                std::string name; file >> name;
                scene(name);
            } else if (action == "camera") {
                file >> angle >> distance >> height >> fov >> centerX >> centerZ;
                anchor = link->current.pos;
                filmCamera = true;
                viewStarted = now;
            } else if (action == "orbit") {
                int on = 0; file >> on; orbit = on != 0; viewStarted = now;
            } else if (action == "follow") {
                filmCamera = false;
                if (auto* cam = dCam_getBody()) cam->Start();
            } else if (action == "move") {
                file >> sx >> sy;
            } else if (action == "button") {
                file >> button; buttonTicks = 2;
            } else if (action == "taunt") {
                match::send_taunt(0);
            } else if (action == "position") {
                float x, y, z; int yaw = 0;
                if (file >> x >> y >> z >> yaw) {
                    link->current.pos = link->old.pos = link->field_0x3798 = cXyz(x,y,z);
                    link->shape_angle.y = link->current.angle.y = static_cast<s16>(yaw);
                    link->speedF = 0;
                }
            }
            mods::log::info("SHOWCASE command {}: {} (room {})", serial, action, net::room_code());
        }
        // Read-only state for the bot choreographer and recording scripts.
        std::ofstream status(std::string(control) + ".status");
        status << net::room_code() << ' ' << static_cast<int>(match::get().phase) << ' '
               << match::get().round << ' ' << static_cast<int>(match::my_role()) << ' '
               << link->current.pos.x << ' ' << link->current.pos.y << ' ' << link->current.pos.z
               << ' ' << match::hiders_left() << '\n';
        std::ofstream groundFile(std::string(control) + ".ground");
        groundFile << "{";
        bool first = true;
        for (int id = 1; id <= kMaxPlayers; ++id) {
            const auto& p = match::player(id);
            if (!p.present || !p.hasState) continue;
            cXyz probe(p.state.x, p.state.y + 500, p.state.z);
            dBgS_LinkGndChk ground;
            ground.SetPos(&probe);
            const float y = dComIfG_Bgsp().GroundCross(&ground);
            if (y < -100000 || !std::isfinite(y)) continue;
            if (!first) groundFile << ',';
            first = false;
            groundFile << '\"' << id << "\":" << y;
        }
        groundFile << "}";
    }
    if (filmCamera) {
        if (auto* cam = dCam_getBody()) {
            const float a = (angle + (orbit ? (now - viewStarted) * 0.001f * 3.0f : 0)) * M_PI / 180;
            const cXyz center(anchor.x + centerX, anchor.y + 100, anchor.z + centerZ);
            const cXyz eye(center.x + std::sin(a) * distance,
                           anchor.y + height, center.z + std::cos(a) * distance);
            cam->Stop();
            cam->SetTrimSize(0);
            cam->Set(center, eye, fov, static_cast<s16>(0));
        }
    }
}
}
