#include "treasure.hpp"
#include "treasure_layout.hpp"
#include "arena.hpp"
#include "common.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_bg_s_lin_chk.h"
#include "d/d_bg_s_wtr_chk.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include <cmath>
#include <chrono>
#include <random>
#include <vector>

namespace hs::treasure {
namespace {
uint32_t s_round = 0;
uint64_t s_nextSpawn = 0;
uint64_t s_nextSeed = 0;
ReachableArea s_area;
std::mt19937 s_rng{std::random_device{}()};

bool ground_position(Point originPoint, Point& atPoint) {
    const cXyz origin(originPoint.x, originPoint.y, originPoint.z);
    cXyz at(atPoint.x, atPoint.y, atPoint.z);
    cXyz probe = at; probe.y = origin.y + 300.0f;
    dBgS_LinkGndChk ground; ground.SetPos(&probe);
    const float y = dComIfG_Bgsp().GroundCross(&ground);
    if (y == -G_CM3D_F_INF || !std::isfinite(y) || std::fabs(y - origin.y) > 260.0f ||
        dComIfG_Bgsp().GetExitId(ground) != 0x3F || dBgS_GetNY(ground) < 0.65f) return false;
    at.y = y;
    if (arena::overlaps_loading_exit({at.x, at.y, at.z})) return false;
    dBgS_WtrChk water; water.Set(at, at.y + 100000.0f);
    if (dComIfG_Bgsp().WaterChk(&water) && water.GetHeight() > at.y + 80.0f) return false;
    // A point over a cliff/wall is a bad reward. Require a clear path at body height and
    // ground beneath intermediate steps; no untested hard-coded coordinates per map.
    cXyz start = origin, end = at; start.y += 80; end.y += 80;
    dBgS_LinkLinChk line; line.Set(&start, &end, dComIfGp_getPlayer(0));
    if (dComIfG_Bgsp().LineCross(&line)) return false;
    const float dx = at.x - origin.x, dz = at.z - origin.z;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dz * dz) / 80.0f)));
    for (int step = 1; step <= steps; ++step) {
        cXyz along = origin + (at - origin) * (static_cast<float>(step) / steps); along.y += 180;
        dBgS_LinkGndChk floor; floor.SetPos(&along);
        const float h = dComIfG_Bgsp().GroundCross(&floor);
        if (h == -G_CM3D_F_INF || !std::isfinite(h) || std::fabs(h - (along.y - 180)) > 140 ||
            dComIfG_Bgsp().GetExitId(floor) != 0x3F ||
            arena::overlaps_loading_exit({along.x, h, along.z}, 35.0f)) return false;
    }
    // Check a pickup-sized footprint, keeping rewards off narrow ledges and away from walls.
    constexpr float offsets[][2] = {{100,0}, {-100,0}, {0,100}, {0,-100}};
    for (const auto& offset : offsets) {
        cXyz edge(at.x + offset[0], at.y + 80.0f, at.z + offset[1]);
        dBgS_LinkGndChk floor; floor.SetPos(&edge);
        const float h = dComIfG_Bgsp().GroundCross(&floor);
        if (h == -G_CM3D_F_INF || !std::isfinite(h) || std::fabs(h - at.y) > 100 ||
            dComIfG_Bgsp().GetExitId(floor) != 0x3F) return false;
        cXyz center = at; center.y += 80.0f;
        dBgS_LinkLinChk clearance; clearance.Set(&center, &edge, dComIfGp_getPlayer(0));
        if (dComIfG_Bgsp().LineCross(&clearance)) return false;
    }
    atPoint = {at.x, at.y, at.z};
    return true;
}
}

void update() {
    const auto& m = match::get();
    if (net::status() != net::Status::Online ||
        (m.phase != Phase::Hide && m.phase != Phase::Seek)) {
        s_round = 0; // a new room can reuse round 1, even on a different map
        return;
    }
    if (!m.settings.treasure ||
        !local::in_world() || dComIfGp_isEnableNextStage() || dComIfGp_event_runCheck() ||
        std::strncmp(local::stage(), map_info(m.map).stage, 8) != 0) return;
    if (m.round != s_round) {
        s_round = m.round; s_nextSpawn = s_nextSeed = 0;
        // Broad outdoor maps use larger steps; village streets and mountain paths need detail.
        const bool broad = m.map == 9 || m.map == 13 || m.map == 14;
        s_area.clear(broad ? 600.0f : 300.0f);
    }
    const uint64_t now = now_ms();
    if (net::is_host()) {
        if (now >= s_nextSeed) {
            s_nextSeed = now + 1000;
            const auto& map = map_info(m.map);
            s_area.seed({map.spawnX, map.spawnY, map.spawnZ}, ground_position);
            // Visited routes supplement the spawn-connected area, including stairs, ladders
            // and newly loaded rooms. Successful nodes persist after players leave that spot.
            for (const auto& p : m.players) {
                if (p.present && p.hasState && (p.state.flags & STATE_IN_WORLD) &&
                    now - p.stateAt < 2000 && std::strncmp(p.state.stage, local::stage(), 8) == 0)
                    s_area.seed({p.state.x, p.state.y, p.state.z}, ground_position);
            }
        }
        // Bound native collision work per frame. Hiding time prepares distant candidates.
        const auto began = std::chrono::steady_clock::now();
        for (int batch = 0; batch < 32 && !s_area.complete(); ++batch) {
            s_area.expand(1, ground_position);
            if (std::chrono::steady_clock::now() - began >= std::chrono::milliseconds(2)) break;
        }
        if (m.phase == Phase::Seek && now >= s_nextSpawn && m.rupeeCount < match::kMaxRupees) {
            s_nextSpawn = now + 350;
            std::vector<Point> occupied;
            for (int i = 0; i < m.rupeeCount; ++i) {
                const auto& r = m.rupees[i]; occupied.push_back({r.x, r.y, r.z});
            }
            const auto& points = s_area.points();
            // A stale candidate can lose its floor when a room unloads or a decoy blocks it.
            // Check again and try the next well-spaced candidate rather than placing blindly.
            for (int attempt = 0; attempt < 12 && !points.empty(); ++attempt) {
                const size_t index = spread_candidate(points, occupied, s_rng() % points.size(), match::kRupeeSpacing,
                    [](Point p) { return !match::rupee_spawn_blocked(p.x, p.z); });
                if (index == points.size()) break;
                Point at = points[index];
                if (ground_position(at, at) && match::spawn_rupee(at.x, at.y, at.z)) break;
                occupied.push_back(at);
            }
        }
    }
    if (m.phase != Phase::Seek || match::my_role() != Role::Hider || match::player(net::self_id()).found) return;
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
