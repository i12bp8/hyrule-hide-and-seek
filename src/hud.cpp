#include "hud.hpp"

#include "common.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "props.hpp"
#include "puppet.hpp"
#include "settings.hpp"

#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "d/d_com_inf_game.h"
#include "d/d_drawlist.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"
#include "m_Do/m_Do_lib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace hs::hud {

namespace {

struct Screen {
    f32 x, y, w, h;
};

Screen screen() {
    return {mDoGph_gInf_c::getMinXF(), mDoGph_gInf_c::getMinYF(), mDoGph_gInf_c::getWidthF(),
        mDoGph_gInf_c::getHeightF()};
}

JUtility::TColor rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return JUtility::TColor(r, g, b, a);
}

class Painter {
public:
    Painter() : m_ortho(0.0f, 0.0f, static_cast<f32>(FB_WIDTH), static_cast<f32>(FB_HEIGHT), -1.0f, 1.0f) {
        const Screen s = screen();
        m_ortho.setOrtho(s.x, s.y, s.w, s.h, -1.0f, 1.0f);
        m_ortho.setPort();
        m_font = mDoExt_getMesgFont();
    }
    ~Painter() {
        if (J2DGrafContext* port = dComIfGp_getCurrentGrafPort()) {
            port->setPort();
            port->setup2D();
        }
    }

    void box(f32 x0, f32 y0, f32 x1, f32 y1, JUtility::TColor c) {
        m_ortho.setPort();
        m_ortho.setColor(c);
        m_ortho.fillBox(JGeometry::TBox2<f32>(x0, y0, x1, y1));
    }

    f32 width(const std::string& text, f32 size) const {
        if (m_font == nullptr) return 0.0f;
        const f32 cell = static_cast<f32>(m_font->getCellWidth());
        f32 w = 0.0f;
        for (const unsigned char c : text) {
            const f32 advance = m_font->isFixed() ? static_cast<f32>(m_font->getFixedWidth())
                                                  : static_cast<f32>(m_font->getWidth(c));
            w += cell > 0.0f ? advance * (size / cell) : size * 0.6f;
        }
        return w;
    }

    // `top` is the top of the text line.
    void text(const std::string& s, f32 x, f32 top, f32 size, JUtility::TColor c, bool shadow = true) {
        if (m_font == nullptr || s.empty()) return;
        m_font->setGX();
        const f32 baseline = top + size * 0.85f;
        if (shadow) {
            const f32 o = std::max(1.0f, size * 0.08f);
            m_font->setCharColor(rgba(0, 0, 0, static_cast<uint8_t>(c.a * 0.75f)));
            m_font->drawString_scale(x + o, baseline + o, size, size, s.c_str(), true);
        }
        m_font->setCharColor(c);
        m_font->drawString_scale(x, baseline, size, size, s.c_str(), true);
    }

    void centered(const std::string& s, f32 cx, f32 top, f32 size, JUtility::TColor c) {
        text(s, cx - width(s, size) * 0.5f, top, size, c);
    }

private:
    J2DOrthoGraph m_ortho;
    JUTFont* m_font = nullptr;
};

std::string clock(uint32_t ms) {
    const uint32_t secs = (ms + 999) / 1000;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u:%02u", secs / 60, secs % 60);
    return buf;
}

JUtility::TColor player_color(int id, uint8_t alpha = 255) {
    const TunicColor& c = color_of(match::player(id).color);
    return rgba(c.r, c.g, c.b, alpha);
}

const char* name_of(int id) {
    return net::member(id).name;
}

void draw_blindfold(Painter& p, const Screen& s) {
    p.box(s.x, s.y, s.x + s.w, s.y + s.h, rgba(4, 4, 10, 250));
    const f32 cx = s.x + s.w * 0.5f;
    const f32 cy = s.y + s.h * 0.5f;
    p.centered("The props are hiding...", cx, cy - 70.0f, 26.0f, rgba(235, 235, 235));
    p.centered(clock(match::ms_left()), cx, cy - 30.0f, 64.0f, rgba(255, 110, 90));
    p.centered("You're a HUNTER. When the timer ends, find them and hit them with your sword.", cx,
        cy + 50.0f, 16.0f, rgba(200, 200, 200));
    if (match::get().settings.mode == Mode::PropHunt) {
        p.centered("Props can be objects or furniture. Swinging at nothing costs one heart.",
            cx, cy + 74.0f, 14.0f, rgba(170, 170, 170));
    }
}

void draw_top(Painter& p, const Screen& s) {
    const match::Match& m = match::get();
    const f32 cx = s.x + s.w * 0.5f;
    std::string label;
    JUtility::TColor color = rgba(255, 255, 255);
    switch (m.phase) {
    case Phase::Lobby:
        label = "Room " + net::room_code();
        color = rgba(255, 230, 140);
        break;
    case Phase::Gather:
        label = "GET READY";
        color = rgba(255, 230, 140);
        break;
    case Phase::Hide:
        label = "HIDE  " + clock(match::ms_left());
        color = rgba(130, 235, 130);
        break;
    case Phase::Seek:
        label = "HUNT  " + clock(match::ms_left());
        color = match::ms_left() < 30000 ? rgba(255, 90, 70) : rgba(255, 190, 120);
        break;
    case Phase::Results:
        label = "ROUND OVER";
        color = rgba(255, 230, 140);
        break;
    }
    const f32 w = std::max(p.width(label, 24.0f), 140.0f) + 30.0f;
    p.box(cx - w * 0.5f, s.y + 8.0f, cx + w * 0.5f, s.y + 44.0f, rgba(0, 0, 0, 150));
    p.centered(label, cx, s.y + 13.0f, 24.0f, color);

    std::string sub;
    if (m.phase == Phase::Lobby) {
        const int n = net::member_count();
        sub = std::to_string(n) + (n == 1 ? " player" : " players") + " - " +
              (net::is_host() ? "start from the Hide & Seek menu" : "waiting for the host");
    } else if (match::in_round()) {
        const bool props = m.settings.mode == Mode::PropHunt;
        sub = std::string(map_info(m.map).name) + " - " + std::to_string(match::hiders_left()) +
              (props ? " props left" : " hiders left");
    }
    if (!sub.empty()) p.centered(sub, cx, s.y + 48.0f, 14.0f, rgba(230, 230, 230, 230));
}

void draw_role(Painter& p, const Screen& s) {
    const match::Match& m = match::get();
    if (!match::in_round()) return;
    const Role role = match::my_role();
    const int me = net::self_id();
    std::string line;
    std::string hint;
    if (role == Role::Hunter) {
        line = "You are a HUNTER";
        hint = m.settings.mode == Mode::PropHunt ? "B: sword / nearby swim tag - follow TAUNT clues"
                                                 : "Touch hiders - follow TAUNT clues";
        if (!local::has_sword() && m.settings.mode == Mode::PropHunt) hint = "No sword! Play from the Hide & Seek save";
    } else if (role == Role::Hider && !match::player(me).found) {
        if (local::disguised()) {
            const match::Player& mePlayer = match::player(me);
            line = std::string("You are a ") + prop_info(local::prop()).name + "  |  " +
                   std::to_string(mePlayer.roundPoints) + " round pts";
            const int free = match::my_decoys_left();
            hint = "D-pad ^ decoy (" +
                   (free > 0 ? std::to_string(free) + " free"
                             : std::to_string(match::kExtraDecoyCost) + " pts") +
                   ")   < > prop";
            if (m.phase == Phase::Seek) hint += "   v taunt (+1, reveals you)";
        } else {
            line = "You are HIDING";
            hint = m.phase == Phase::Seek ? "D-pad v taunt (+1, reveals you)" : "";
        }
    } else if (role == Role::Spectator || match::player(me).found) {
        line = "Spectating";
    }
    if (line.empty()) return;
    const f32 x = s.x + 16.0f;
    const f32 y = s.y + s.h - 62.0f;
    p.box(x - 8.0f, y - 6.0f, x + std::max(p.width(line, 20.0f), p.width(hint, 13.0f)) + 10.0f, y + 46.0f,
        rgba(0, 0, 0, 130));
    p.text(line, x, y, 20.0f, player_color(me));
    p.text(hint, x, y + 25.0f, 13.0f, rgba(225, 225, 225));
    if (role == Role::Hider && m.phase == Phase::Seek && local::taunt_cooldown() > 0.0f) {
        const f32 w = 90.0f * local::taunt_cooldown();
        p.box(x, y + 41.0f, x + w, y + 43.0f, rgba(255, 230, 140, 200));
    }
}

void draw_feed(Painter& p, const Screen& s) {
    f32 y = s.y + 70.0f;
    for (const match::Notice& n : match::notices()) {
        if (n.big) continue;
        p.text(n.text, s.x + 16.0f, y, 15.0f, rgba(n.r, n.g, n.b, 235));
        y += 20.0f;
    }
}

void draw_banner(Painter& p, const Screen& s) {
    const match::Notice* latest = nullptr;
    for (const match::Notice& n : match::notices()) {
        if (n.big) latest = &n;
    }
    if (latest == nullptr) return;
    const uint64_t age = now_ms() - latest->at;
    if (age > 3500) return;
    // Pop in, hold, fade out.
    const f32 t = static_cast<f32>(age) / 1000.0f;
    const f32 scale = t < 0.15f ? 0.6f + t / 0.15f * 0.4f : 1.0f;
    const uint8_t alpha = age > 2800 ? static_cast<uint8_t>(255 * (3500 - age) / 700) : 255;
    const f32 size = 44.0f * scale;
    p.centered(latest->text, s.x + s.w * 0.5f, s.y + s.h * 0.30f, size,
        rgba(latest->r, latest->g, latest->b, alpha));
}

void draw_name_tags(Painter& p, const Screen& s) {
    const match::Match& m = match::get();
    const Role myRole = match::my_role();
    const bool namesEnabled = settings::name_tags();
    const view_class* view = dComIfGd_getView();
    if (view == nullptr) return;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (id == net::self_id()) continue;
        const match::Player& pl = match::player(id);
        cXyz feet;
        float height;
        if (!puppet::anchor(id, feet, height)) {
            // Even a failed puppet allocation must not make a hunter untrackable to hiders.
            if (!pl.present || !pl.hasState || !(pl.state.flags & STATE_IN_WORLD) ||
                now_ms() - pl.stateAt >= 4000 || std::strncmp(pl.state.stage, local::stage(), 8) != 0) {
                continue;
            }
            feet.set(pl.state.x, pl.state.y, pl.state.z);
            height = (pl.state.flags & STATE_DISGUISED) ? prop_info(pl.state.prop).height : 150.0f;
        }
        // A hider must always be able to identify the threat. This also provides a reliable
        // fallback if a remote Link model cannot be drawn after a stage transition.
        const bool hunterMarker = match::in_round() && myRole == Role::Hider && pl.role == Role::Hunter;
        if (!namesEnabled && !hunterMarker) continue;
        const bool hiding = match::in_round() && pl.role == Role::Hider && !pl.found;
        cXyz head = feet;
        head.y += height + 35.0f;
        const f32 dist = (head - view->lookat.eye).abs();
        if (hiding && myRole != Role::Hider) {
            // The whole point: hunters don't get told where props are. In Hide & Seek a hider's
            // name shows up once you're close.
            if (m.settings.mode == Mode::PropHunt || dist > 500.0f) continue;
        }
        if (dist > 5000.0f) continue;
        Vec cam;
        mDoLib_pos2camera(&head, &cam);
        if (cam.z > -1.0f) continue;  // behind us
        Vec out;
        mDoLib_project(&head, &out);
        if (out.x < s.x || out.x > s.x + s.w || out.y < s.y || out.y > s.y + s.h) continue;
        const f32 size = std::clamp(18.0f * 600.0f / std::max(dist, 1.0f), 9.0f, 18.0f);
        std::string label = name_of(id);
        if (match::in_round() && pl.role == Role::Hunter) label += " [HUNTER]";
        p.centered(label, out.x, out.y - size, hunterMarker ? std::max(size, 14.0f) : size,
            hunterMarker ? rgba(255, 105, 75) : player_color(id));
    }
}

void draw_taunt_pings(Painter& p, const Screen& s) {
    if (match::my_role() != Role::Hunter || match::get().phase != Phase::Seek) return;
    const view_class* view = dComIfGd_getView();
    if (view == nullptr) return;

    struct Ping {
        int id;
        cXyz position;
        Vec camera;
        float strength;
        float distance;
    };
    std::vector<Ping> pings;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        cXyz position;
        const float strength = local::taunt_ping(id, position);
        if (strength <= 0.0f) continue;
        Vec camera;
        mDoLib_pos2camera(&position, &camera);
        pings.push_back({id, position, camera, strength, (position - view->lookat.eye).abs()});
    }
    if (pings.empty()) return;
    std::sort(pings.begin(), pings.end(),
        [](const Ping& a, const Ping& b) { return a.strength > b.strength; });

    const f32 cx = s.x + s.w * 0.5f;
    const int lines = std::min<int>(3, pings.size());
    for (int i = 0; i < lines; ++i) {
        const Ping& ping = pings[i];
        const float side = -ping.camera.z * 0.35f;
        const char* direction = ping.camera.z > -1.0f ? "BEHIND"
                                : ping.camera.x > side ? "RIGHT"
                                : ping.camera.x < -side ? "LEFT"
                                                       : "AHEAD";
        const int metres = std::max(1, static_cast<int>(std::lround(ping.distance / 100.0f)));
        const std::string clue = std::string("TAUNT: ") + name_of(ping.id) + " - " + direction +
                                 " - " + std::to_string(metres) + "m";
        const uint8_t alpha = static_cast<uint8_t>(100.0f + 155.0f * ping.strength);
        const f32 y = s.y + 69.0f + static_cast<f32>(i) * 22.0f;
        const f32 w = p.width(clue, 18.0f) + 24.0f;
        p.box(cx - w * 0.5f, y - 3.0f, cx + w * 0.5f, y + 21.0f, rgba(0, 0, 0, alpha / 2));
        p.centered(clue, cx, y, 18.0f, rgba(255, 205, 65, alpha));
    }

    // Projected markers are intentionally visible through scenery for five seconds. A hider gets
    // a point for taking this risk, and hunters get a clue that remains useful across large maps.
    for (const Ping& ping : pings) {
        if (ping.camera.z > -1.0f) continue;
        cXyz marker = ping.position;
        const match::Player& pl = match::player(ping.id);
        marker.y += (pl.hasState && (pl.state.flags & STATE_DISGUISED))
                        ? prop_info(pl.state.prop).height + 35.0f
                        : 185.0f;
        Vec out;
        mDoLib_project(&marker, &out);
        if (out.x < s.x + 10.0f || out.x > s.x + s.w - 10.0f || out.y < s.y + 10.0f ||
            out.y > s.y + s.h - 10.0f) {
            continue;
        }
        const uint8_t alpha = static_cast<uint8_t>(80.0f + 175.0f * ping.strength);
        const f32 pulse = 20.0f + 2.0f * std::sin(static_cast<f32>(now_ms() % 1000) * 0.012f);
        p.centered(std::string("! TAUNT: ") + name_of(ping.id) + " !", out.x, out.y - pulse,
            pulse, rgba(255, 205, 65, alpha));
    }
}

void draw_scoreboard(Painter& p, const Screen& s) {
    const match::Match& m = match::get();
    std::vector<int> ids;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (match::player(id).present) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [](int a, int b) {
        const match::Player& pa = match::player(a);
        const match::Player& pb = match::player(b);
        return pa.roundPoints != pb.roundPoints ? pa.roundPoints > pb.roundPoints : pa.score > pb.score;
    });
    const f32 rowH = 22.0f;
    const f32 w = 420.0f;
    const f32 h = 70.0f + rowH * static_cast<f32>(ids.size());
    const f32 x0 = s.x + (s.w - w) * 0.5f;
    const f32 y0 = s.y + s.h * 0.42f;
    p.box(x0, y0, x0 + w, y0 + h, rgba(0, 0, 0, 185));
    const char* title = m.winner == 1 ? "Hunters win!" : (m.settings.mode == Mode::PropHunt ? "Props win!" : "Hiders win!");
    p.centered(title, x0 + w * 0.5f, y0 + 8.0f, 24.0f, m.winner == 1 ? rgba(255, 110, 90) : rgba(130, 235, 130));
    p.text("Player", x0 + 16.0f, y0 + 42.0f, 13.0f, rgba(180, 180, 180));
    p.text("Round", x0 + w - 150.0f, y0 + 42.0f, 13.0f, rgba(180, 180, 180));
    p.text("Total", x0 + w - 70.0f, y0 + 42.0f, 13.0f, rgba(180, 180, 180));
    f32 y = y0 + 62.0f;
    for (int id : ids) {
        const match::Player& pl = match::player(id);
        std::string name = name_of(id);
        if (pl.role == Role::Hunter && !pl.found) name += "  (hunter)";
        else if (pl.found) name += "  (found)";
        p.text(name, x0 + 16.0f, y, 16.0f, player_color(id));
        p.text("+" + std::to_string(pl.roundPoints), x0 + w - 150.0f, y, 16.0f, rgba(255, 230, 140));
        p.text(std::to_string(pl.score), x0 + w - 70.0f, y, 16.0f, rgba(240, 240, 240));
        y += rowH;
    }
}

class HudDlst : public dDlst_base_c {
public:
    void draw() override {
        Painter p;
        const Screen s = screen();
        if (local::blindfolded()) {
            draw_blindfold(p, s);
            draw_feed(p, s);
            return;
        }
        draw_name_tags(p, s);
        draw_top(p, s);
        draw_taunt_pings(p, s);
        draw_role(p, s);
        draw_feed(p, s);
        if (match::get().phase == Phase::Results) draw_scoreboard(p, s);
        draw_banner(p, s);
    }
};

HudDlst s_dlst;

}  // namespace

void queue() {
    if (net::status() != net::Status::Online || !local::in_world()) return;
    dDlst_list_c& lists = g_dComIfG_gameInfo.drawlist;
    for (dDlst_base_c** it = lists.mp2DXluDrawLists; it < lists.mp2DXluStart; ++it) {
        if (*it == &s_dlst) return;
    }
    dComIfGd_set2DXlu(&s_dlst);
}

}  // namespace hs::hud
