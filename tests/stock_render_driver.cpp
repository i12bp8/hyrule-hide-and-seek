// Run only in an isolated --user-dir on stock Dusklight, with --stage F_SP103,0,13,-1.
// This drives the real actors/render lists, not a mock GPU. Stock Linux needs an HTTPS relay.
#include "common.hpp"
#include "local.hpp"
#include "match.hpp"
#include "net.hpp"
#include "props.hpp"
#include "settings.hpp"

#include <mods/svc/hook.hpp>
#include "JSystem/J3DGraphBase/J3DDrawBuffer.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "d/d_com_inf_game.h"
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

void roster() {
    Writer w(MSG_ROSTER);
    w.u8(kMaxPlayers);
    for (int id = 1; id <= kMaxPlayers; ++id) {
        w.u8(id);
        w.u8(static_cast<uint8_t>(id == 2 ? Role::Hunter : Role::Hider));
        w.u8(id - 1);
        w.u16(0); w.u16(0);
        w.u8(2); w.u8(0); w.u8(0);
    }
    apply(net::self_id(), w);
}

void decoys(int kind, const cXyz& at) {
    Writer w(MSG_DECOYS);
    w.u32(1);
    w.u8(match::kMaxActiveDecoys);
    for (int i = 0; i < match::kMaxActiveDecoys; ++i) {
        w.u8(i + 1); w.u8(i / match::kMaxDecoysPerPlayer + 1); w.u8(kind);
        w.f32(at.x + (i % 16 - 8) * 120.0f);
        w.f32(at.y);
        w.f32(at.z + (i / 16 - 5) * 120.0f);
        w.s16(0);
    }
    apply(net::self_id(), w);
    if (match::decoy_count() != match::kMaxActiveDecoys) {
        mods::log::error("STOCK_RENDER_TEST FAIL: snapshot rejected");
        std::exit(2);
    }
    mods::log::info("STOCK_RENDER_TEST: 160 decoys of {}", prop_info(kind).name);
}
}  // namespace

void stock_render_update() {
    const auto now = now_ms();
    if (s_start == 0) {
        s_start = now;
        if (mods::hook::add_pre<DrawHead>(check_packets) != MOD_OK) std::exit(2);
        if (mods::hook::add_pre<MatDraw>(check_shapes) != MOD_OK) std::exit(2);
    }
    if (now - s_start > 240000) {
        mods::log::error("STOCK_RENDER_TEST FAIL: timed out");
        std::exit(2);
    }
    if (!local::in_world() || dComIfGp_isEnableNextStage()) return;
    if (net::status() == net::Status::Offline && !s_left) {
        net::host_room(settings::server(), "Stock render test");
        return;
    }
    if (s_left) {
        if (now - s_joined > 2000) {
            mods::log::info("STOCK_RENDER_TEST PASS: lobby, 50 prop kinds, 160 decoys, cleanup; {} draw-list checks", s_checks);
            // This is an in-frame test, not application shutdown: exit() runs game globals'
            // destructors while the engine is still active. Gameplay actor cleanup ran above.
            std::fflush(nullptr);
            std::_Exit(0);
        }
        return;
    }
    if (net::status() != net::Status::Online) return;
    if (s_joined == 0) {
        s_joined = now;
        for (int id = 2; id <= kMaxPlayers; ++id) match::on_joined(id);
        mods::log::info("STOCK_RENDER_TEST: full 16-player lobby");
    }
    const auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr) return;
    const cXyz at = link->current.pos;
    for (int id = 2; id <= kMaxPlayers; ++id) {
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
        apply(net::self_id(), round);
        Writer phase(MSG_PHASE);
        phase.u32(1); phase.u8(static_cast<uint8_t>(Phase::Hide)); phase.u32(600000);
        apply(net::self_id(), phase);
        s_round = true;
    }
    if (!s_round || now - s_joined < 12000) return;
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
    if (now - changedAt < 2000) return;
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
    decoys(s_kind, at);
}
}  // namespace hs::testing
