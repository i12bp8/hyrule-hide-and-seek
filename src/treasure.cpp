#include "treasure.hpp"
#include "common.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_bg_s_lin_chk.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include <cmath>
#include <random>
#include <vector>

namespace hs::treasure {
namespace {
uint32_t s_round = 0;
uint64_t s_nextSpawn = 0;
std::mt19937 s_rng{std::random_device{}()};

bool ground_position(const cXyz& origin, cXyz& at) {
    cXyz probe = at; probe.y = origin.y + 300.0f;
    dBgS_LinkGndChk ground; ground.SetPos(&probe);
    const float y = dComIfG_Bgsp().GroundCross(&ground);
    if (y == -G_CM3D_F_INF || !std::isfinite(y) || std::fabs(y - origin.y) > 260.0f ||
        dComIfG_Bgsp().GetExitId(ground) != 0x3F) return false;
    at.y = y;
    // A point over a cliff/wall is a bad reward. Require a clear path at body height and
    // ground beneath intermediate steps; no untested hard-coded coordinates per map.
    cXyz start = origin, end = at; start.y += 80; end.y += 80;
    dBgS_LinkLinChk line; line.Set(&start, &end, dComIfGp_getPlayer(0));
    if (dComIfG_Bgsp().LineCross(&line)) return false;
    for (int step = 1; step <= 4; ++step) {
        cXyz along = origin + (at - origin) * (step / 4.0f); along.y += 180;
        dBgS_LinkGndChk floor; floor.SetPos(&along);
        const float h = dComIfG_Bgsp().GroundCross(&floor);
        if (h == -G_CM3D_F_INF || std::fabs(h - (along.y - 180)) > 140 ||
            dComIfG_Bgsp().GetExitId(floor) != 0x3F) return false;
    }
    return true;
}
}

void update() {
    const auto& m = match::get();
    if (net::status() != net::Status::Online || m.phase != Phase::Seek || !m.settings.treasure ||
        !local::in_world() || dComIfGp_isEnableNextStage() || dComIfGp_event_runCheck() ||
        std::strncmp(local::stage(), map_info(m.map).stage, 8) != 0) return;
    if (m.round != s_round) { s_round = m.round; s_nextSpawn = 0; }
    const uint64_t now = now_ms();
    if (net::is_host() && now >= s_nextSpawn && m.rupeeCount < match::kMaxRupees) {
        s_nextSpawn = now + (m.rupeeCount < 3 ? 1000 : 5000);
        std::vector<cXyz> anchors;
        for (const auto& p : m.players) {
            if (p.present && p.hasState && (p.state.flags & STATE_IN_WORLD) &&
                now - p.stateAt < 2000 && std::strncmp(p.state.stage, local::stage(), 8) == 0)
                anchors.emplace_back(p.state.x, p.state.y, p.state.z);
        }
        if (!anchors.empty()) for (int attempt = 0; attempt < 12; ++attempt) {
            const cXyz origin = anchors[s_rng() % anchors.size()];
            const float angle = std::uniform_real_distribution<float>(0, 6.2831853f)(s_rng);
            const float distance = std::uniform_real_distribution<float>(650, 1800)(s_rng);
            cXyz at(origin.x + std::sin(angle) * distance, origin.y, origin.z + std::cos(angle) * distance);
            if (ground_position(origin, at) && match::spawn_rupee(at.x, at.y, at.z)) break;
        }
    }
    if (match::my_role() != Role::Hider || match::player(net::self_id()).found) return;
    const auto& p = match::player(net::self_id()).state;
    for (int i = 0; i < m.rupeeCount; ++i) {
        const auto& r = m.rupees[i];
        const float dx = p.x - r.x, dz = p.z - r.z;
        if (r.expiresAt > now && dx * dx + dz * dz <= 100.0f * 100.0f && std::fabs(p.y - r.y) <= 100) {
            match::collect_rupee(r.id); break;
        }
    }
}
}
