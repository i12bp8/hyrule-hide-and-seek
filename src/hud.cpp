#include "hud.hpp"
#include "hud_layout.hpp"

#include "common.hpp"
#include "local.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "props.hpp"
#include "puppet.hpp"
#include "settings.hpp"
#include "scoring.hpp"
#include "ui.hpp"

#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "d/d_com_inf_game.h"
#include "d/d_drawlist.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"
#include "m_Do/m_Do_lib.h"

#include <algorithm>
#include <array>
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

    // Draw a filled arrow with the same native 2D primitives as the cards. The game font does
    // not reliably contain Unicode arrow glyphs, so this works on every platform and language.
    void arrow(f32 cx, f32 cy, const SearchClue& clue, JUtility::TColor color) {
        const f32 dx = clue.arrowX, dy = clue.arrowY;
        const auto strip = [&](f32 start, f32 end, f32 halfWidth) {
            const f32 x0 = cx + dx * start + dy * halfWidth;
            const f32 y0 = cy + dy * start - dx * halfWidth;
            const f32 x1 = cx + dx * end - dy * halfWidth;
            const f32 y1 = cy + dy * end + dx * halfWidth;
            box(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1), color);
        };
        strip(-7, 1, 1.5f);
        for (int step = 0; step < 4; ++step)
            strip(6 - step * 2, 8 - step * 2, 1.5f * (step + 1));
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

    void card(const Rect& r, JUtility::TColor color) {
        const float radius = std::min(4.0f, r.h * 0.25f);
        box(r.x + radius, r.y, r.x + r.w - radius, r.y + 1, color);
        box(r.x + 2, r.y + 1, r.x + r.w - 2, r.y + radius, color);
        box(r.x, r.y + radius, r.x + r.w, r.y + r.h - radius, color);
        box(r.x + 2, r.y + r.h - radius, r.x + r.w - 2, r.y + r.h - 1, color);
        box(r.x + radius, r.y + r.h - 1, r.x + r.w - radius, r.y + r.h, color);
    }

    f32 fit(const std::string& text, f32 size, f32 maximum) const {
        const auto measured = width(text, size);
        return measured > maximum && measured > 0 ? size * maximum / measured : size;
    }

    std::string shortened(std::string text, f32 size, f32 maximum) const {
        if (width(text, size) <= maximum) return text;
        while (!text.empty() && width(text + "...", size) > maximum) {
            size_t start = text.size() - 1;
            while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) --start;
            text.resize(start);
        }
        return text.empty() ? "" : text + "...";
    }

    void centred_fit(const std::string& text, f32 cx, f32 top, f32 size, f32 maximum, JUtility::TColor color) {
        centered(text, cx, top, fit(text, size, maximum), color);
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

std::array<Rect, kMaxPlayers * 2 + 2> s_labels; // names, two treasure labels, all final markers
int s_labelCount = 0;

bool reserve_label(const Rect& r, const Screen& s) {
    if (r.x < s.x + 8 || r.x + r.w > s.x + s.w - 8 || r.y < s.y + 102 ||
        r.y + r.h > s.y + s.h - 88 || s_labelCount == static_cast<int>(s_labels.size())) return false;
    for (int i = 0; i < s_labelCount; ++i) if (r.overlaps(s_labels[i])) return false;
    s_labels[s_labelCount++] = r;
    return true;
}

void draw_blindfold(Painter& p, const Screen& s) {
    p.box(s.x, s.y, s.x + s.w, s.y + s.h, rgba(4, 4, 10, 250));
    const f32 cx = s.x + s.w * 0.5f, cy = s.y + s.h * 0.5f;
    p.centred_fit("HUNTER", cx, cy - 60, 15, s.w - 40, rgba(255, 110, 90));
    p.centred_fit(clock(match::ms_left()), cx, cy - 30, 44, s.w - 40, rgba(235, 235, 235));
    p.centred_fit("Let them hide. Your hunt starts soon.", cx, cy + 34, 12, s.w - 40, rgba(200, 200, 200));
}

void draw_top(Painter& p, const Screen& s) {
    const auto& m = match::get();
    std::string title, detail;
    auto accent = rgba(255, 230, 140);
    switch (m.phase) {
    case Phase::Lobby:
        title = "ROOM " + net::room_code();
        detail = std::to_string(net::member_count()) + "/16 players";
        break;
    case Phase::Gather: title = "GET READY"; detail = map_info(m.map).name; break;
    case Phase::Hide:
        title = "HIDE  " + clock(match::ms_left()); detail = map_info(m.map).name;
        accent = rgba(130, 235, 130); break;
    case Phase::Seek:
        title = "HUNT  " + clock(match::ms_left()) + "  |  " + std::to_string(match::hiders_left()) + " LEFT";
        detail = map_info(m.map).name;
        accent = match::ms_left() <= 30000 ? rgba(255, 90, 70) : rgba(255, 190, 120);
        break;
    case Phase::Results: title = "ROUND " + std::to_string(m.round); detail = "Results"; break;
    }
    const auto card = centre_card(s.x, s.y, s.w, 8, 40, 284);
    p.card(card, rgba(0, 0, 0, 160));
    const auto cx = card.x + card.w * 0.5f;
    p.centred_fit(title, cx, card.y + 5, 16, card.w - 20, accent);
    p.centred_fit(detail, cx, card.y + 25, 10, card.w - 20, rgba(230, 230, 230));
}

void draw_role(Painter& p, const Screen& s) {
    const auto& m = match::get();
    if (!match::in_round()) return;
    const auto& me = match::player(net::self_id());
    std::string title, detail;
    if (me.role == Role::Hunter) {
        title = "HUNTER";
        if (match::my_hunter_life() == 0) { title = "OUT OF HEARTS"; detail = "Watching until next round"; }
        else if (m.phase == Phase::Seek && m.settings.trackingPulse) {
            const auto seconds = local::tracking_cooldown_secs();
            detail = seconds == 0 ? "Tracking ready" : "Tracking  " + std::to_string(seconds) + "s";
        } else detail = "Find every hider";
    } else if (me.role == Role::Hider && !me.found) {
        title = local::disguised() ? prop_info(local::prop()).name : "HIDING";
        if (local::disguised()) detail = "Decoys " + std::to_string(match::my_decoys_left());
        if (m.phase == Phase::Seek && m.settings.treasure) {
            if (!detail.empty()) detail += "  |  ";
            detail += me.rupeesCollected < 3 ? "Loot " + std::to_string(me.rupeesCollected) + "/3" : "Loot complete";
        }
        if (detail.empty()) detail = m.phase == Phase::Hide ? "Find your spot" : "Stay alert";
    } else { title = me.eliminated ? "OUT OF HEARTS" : "WATCHING"; detail = "Next round soon"; }
    const auto card = centre_card(s.x, s.y, s.w, s.h - 56, 40, 300);
    p.card(card, rgba(0, 0, 0, 150));
    const std::string score = std::to_string(me.roundPoints) + " pts";
    const f32 scoreWidth = p.width(score, 11);
    p.text(p.shortened(title, 13, std::max(0.0f, card.w - scoreWidth - 35)), card.x + 12, card.y + 5, 13, player_color(net::self_id()));
    p.text(score, card.x + card.w - scoreWidth - 12, card.y + 6, 11, rgba(255, 230, 140));
    p.centred_fit(detail, card.x + card.w * 0.5f, card.y + 25, 10, card.w - 20, rgba(225, 225, 225));
    if (settings::control_hints()) {
        const char* hint = me.role == Role::Spectator ? "Watching until next round" : me.role == Role::Hunter
            ? (m.settings.mode == Mode::PropHunt ? "B: sword / swim tag  |  Down: track" : "Touch hiders  |  Down: track")
            : (m.settings.mode == Mode::PropHunt ? "D-pad: < > prop, up decoy, down taunt" : "Hide and relocate  |  D-pad down: taunt");
        p.centred_fit(hint, s.x + s.w * 0.5f, card.y - 16, 10, std::min(s.w - 24, 320.0f), rgba(225, 225, 225));
    }
}

void draw_hider_feedback(Painter& p, const Screen& s) {
    const auto& m = match::get();
    const auto& me = match::player(net::self_id());
    if (m.phase != Phase::Seek || me.role != Role::Hider || me.found) return;
    const f32 cx = s.x + s.w * 0.5f;
    const auto now = now_ms();
    if (me.revealedUntil > now || me.finalRevealedUntil > now) {
        const bool finalActive = me.finalRevealedUntil > now;
        const auto kind = finalActive ? ClueKind::Final : local::taunt_kind();
        const auto until = finalActive ? me.finalRevealedUntil : me.revealedUntil;
        const char* reason = kind == ClueKind::Manual ? "Manual taunt" : kind == ClueKind::Stationary ?
            "Stayed still too long" : kind == ClueKind::Treasure ? "Treasure pickup" : "Final location reveal";
        const auto card = centre_card(s.x, s.y, s.w, 55, 39, 284);
        const uint8_t opacity = static_cast<uint8_t>(195 + 15 * std::sin(now % 1200 * 0.005236f));
        p.card(card, rgba(120, 45, 15, opacity));
        p.box(card.x + 1, card.y + 6, card.x + 3, card.y + card.h - 6, rgba(255, 205, 65));
        const char* alert = kind == ClueKind::Final ? "LOCATION REVEALED  " : "CLUE SENT  ";
        p.centred_fit(alert + std::to_string((until - now + 999) / 1000) + "s", cx, card.y + 5, 14, card.w - 22, rgba(255, 230, 140));
        p.centred_fit(reason, cx, card.y + 25, 10, card.w - 22, rgba(255, 235, 195));
    } else {
        const auto next = match::next_clue_ms();
        if (next != UINT32_MAX) {
            const std::string text = "Clue in " + std::to_string((next + 999) / 1000) + "s";
            p.centred_fit(text, cx, s.y + 56, next <= 3000 ? 13 : 10, s.w * 0.56f, next <= 3000 ? rgba(255, 170, 90) : rgba(240, 230, 180));
        }
    }
}

void draw_treasure(Painter& p, const Screen& s) {
    const auto& m = match::get();
    if (m.phase != Phase::Seek || !m.settings.treasure || match::my_role() != Role::Hider ||
        std::strncmp(local::stage(), map_info(m.map).stage, 8) != 0) return;
    const auto* view = dComIfGd_getView();
    if (view == nullptr) return;
    std::vector<std::pair<float, int>> nearby;
    for (int i = 0; i < m.rupeeCount; ++i) {
        const auto& r = m.rupees[i];
        const float distance = (cXyz(r.x, r.y, r.z) - view->lookat.eye).abs();
        if (r.expiresAt > now_ms() && distance < 1400) nearby.emplace_back(distance, i);
    }
    std::sort(nearby.begin(), nearby.end());
    int drawn = 0;
    for (const auto& candidate : nearby) {
        if (drawn == 2) break;
        const auto& r = m.rupees[candidate.second];
        cXyz at(r.x, r.y + 80, r.z);
        Vec camera; mDoLib_pos2camera(&at, &camera); if (camera.z > -1) continue;
        Vec out; mDoLib_project(&at, &out);
        const int points = scoring::bonus(match::player(net::self_id()).bonusEarned, match::rupee_points());
        const std::string text = points > 0 ? "+" + std::to_string(points) : "Loot";
        const Rect label{out.x - 17, out.y - 12, 34, 18};
        if (!reserve_label(label, s)) continue;
        p.card(label, rgba(0, 0, 0, 150));
        p.centered(text, out.x, label.y + 3, 10, rgba(110, 255, 155));
        ++drawn;
    }
}

void draw_feed(Painter& p, const Screen& s) {
    if (local::blindfolded() || match::get().phase == Phase::Results) return;
    const match::Notice* latest = nullptr;
    for (const auto& notice : match::notices()) {
        if (notice.big && now_ms() - notice.at < 3500) return;
        if (!notice.big) latest = &notice;
    }
    if (latest == nullptr || now_ms() - latest->at > 2800) return;
    const auto card = centre_card(s.x, s.y, s.w, s.h - 98, 21, 310);
    p.card(card, rgba(0, 0, 0, 140));
    p.centered(p.shortened(latest->text, 10, card.w - 20), card.x + card.w * 0.5f, card.y + 5, 10, rgba(230, 230, 230));
}

void draw_banner(Painter& p, const Screen& s) {
    const match::Notice* latest = nullptr;
    for (const match::Notice& n : match::notices()) {
        if (n.big) latest = &n;
    }
    if (latest == nullptr || match::get().phase == Phase::Results) return;
    const uint64_t age = now_ms() - latest->at;
    if (age > 3500) return;
    // Pop in, hold, fade out.
    const f32 t = static_cast<f32>(age) / 1000.0f;
    const f32 scale = t < 0.15f ? 0.6f + t / 0.15f * 0.4f : 1.0f;
    const uint8_t alpha = age > 2800 ? static_cast<uint8_t>(255 * (3500 - age) / 700) : 255;
    const f32 size = p.fit(latest->text, 28.0f * scale, s.w - 48);
    p.centered(latest->text, s.x + s.w * 0.5f, s.y + s.h * 0.30f, size,
        rgba(latest->r, latest->g, latest->b, alpha));
}

void draw_name_tags(Painter& p, const Screen& s) {
    const auto& m = match::get();
    const auto mine = match::my_role();
    const auto* view = dComIfGd_getView();
    if (view == nullptr) return;
    struct Tag { int id; cXyz head; float distance; bool hunter; };
    std::vector<Tag> tags;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const auto& player = match::player(id);
        if (id == net::self_id() || !player.present) continue;
        const bool hunter = match::in_round() && mine == Role::Hider && player.role == Role::Hunter;
        if (!settings::name_tags() && !hunter) continue;
        cXyz feet; float height;
        if (!puppet::anchor(id, feet, height)) continue;
        cXyz head = feet; head.y += height + 28;
        const float distance = (head - view->lookat.eye).abs();
        if (match::in_round() && player.role == Role::Hider && !player.found && mine != Role::Hider &&
            m.settings.mode == Mode::PropHunt) continue;
        tags.push_back({id, head, distance, hunter});
    }
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) {
        return a.hunter != b.hunter ? a.hunter : a.distance < b.distance;
    });
    for (const auto& tag : tags) {
        cXyz head = tag.head;
        Vec camera; mDoLib_pos2camera(&head, &camera); if (camera.z > -1) continue;
        Vec out; mDoLib_project(&head, &out);
        const float size = tag.hunter ? 11.0f : 10.0f;
        const std::string label = tag.hunter ? "Hunter" : p.shortened(name_of(tag.id), size, 92);
        if (label.empty()) continue;
        const float width = p.width(label, size) + 14;
        const Rect card{out.x - width * 0.5f, out.y - 18, width, 19};
        if (!reserve_label(card, s)) continue;
        p.card(card, rgba(0, 0, 0, tag.hunter ? 160 : 130));
        p.centered(label, out.x, card.y + 4, size, tag.hunter ? rgba(255, 105, 75) : player_color(tag.id));
    }
}

void draw_final_markers(Painter& p, const Screen& s) {
    if (match::my_role() != Role::Hunter || match::get().phase != Phase::Seek ||
        dComIfGd_getView() == nullptr) return;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        cXyz at;
        const float strength = local::final_clue_marker(id, at);
        if (strength <= 0) continue;
        Vec camera; mDoLib_pos2camera(&at, &camera);
        if (camera.z > -1) continue;
        Vec out; mDoLib_project(&at, &out);
        const Rect marker{out.x - 8, out.y - 16, 16, 20};
        if (!reserve_label(marker, s)) continue;
        const auto alpha = static_cast<uint8_t>(140 + strength * 100);
        p.card(marker, rgba(0, 0, 0, alpha));
        p.centered("!", out.x, marker.y + 4, 13, rgba(255, 150, 70, alpha));
    }
}

void draw_taunt_pings(Painter& p, const Screen& s) {
    if (match::my_role() != Role::Hunter || match::get().phase != Phase::Seek) return;
    struct Ping { SearchClue clue; float strength; };
    std::vector<Ping> pings;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        SearchClue clue; const auto strength = local::taunt_ping(id, clue);
        if (strength <= 0) continue;
        pings.push_back({clue, strength});
    }
    std::sort(pings.begin(), pings.end(), [](const Ping& a, const Ping& b) {
        return a.strength > b.strength;
    });
    std::string text;
    SearchClue shown;
    bool tracking = false;
    if (!pings.empty()) {
        const auto& ping = pings.front();
        shown = ping.clue;
        text = std::string("Clue  |  ") + ping.clue.direction + "  |  " + ping.clue.range;
        if (pings.size() > 1) text += "  +" + std::to_string(pings.size() - 1);
    } else {
        SearchClue clue;
        if (!local::tracking_clue(clue)) return;
        shown = clue;
        text = std::string("Tracking  |  ") + clue.direction + "  |  " + clue.range;
        tracking = true;
    }
    const auto card = centre_card(s.x, s.y, s.w, 55, 25, 284);
    p.card(card, rgba(0, 0, 0, 160));
    const auto color = tracking ? rgba(120, 230, 255) : rgba(255, 205, 65);
    p.arrow(card.x + 16, card.y + 12, shown, color);
    p.centred_fit(text, card.x + card.w * 0.5f + 10, card.y + 6, 12, card.w - 44, color);
}

void draw_scoreboard(Painter& p, const Screen& s) {
    const auto& m = match::get();
    std::vector<int> ids;
    for (int id = 1; id <= kMaxPlayers; ++id) if (match::player(id).present) ids.push_back(id);
    std::sort(ids.begin(), ids.end(), [](int a, int b) {
        const auto& first = match::player(a); const auto& second = match::player(b);
        return first.roundPoints != second.roundPoints ? first.roundPoints > second.roundPoints : first.score > second.score;
    });
    const auto layout = score_layout(s.x, s.y, s.w, s.h, ids.size());
    const auto& card = layout.card;
    p.card(card, rgba(0, 0, 0, 215));
    const char* title = m.winner == 1 ? "Hunters win" : m.settings.mode == Mode::PropHunt ? "Props win" : "Hiders win";
    p.centred_fit(title, card.x + card.w * 0.5f, card.y + 9, 18, card.w - 24,
        m.winner == 1 ? rgba(255, 110, 90) : rgba(130, 235, 130));
    const float loot = card.x + card.w * 0.51f, finds = card.x + card.w * 0.62f;
    const float round = card.x + card.w * 0.75f, total = card.x + card.w * 0.89f;
    const auto cell = [&](const std::string& text, float x, float y, float size, JUtility::TColor color) {
        p.centred_fit(text, x, y, size, card.w * 0.10f, color);
    };
    p.text("Player", card.x + 12, card.y + 36, 10, rgba(180, 180, 180));
    cell("Loot", loot, card.y + 36, 10, rgba(180, 180, 180));
    cell("Finds", finds, card.y + 36, 10, rgba(180, 180, 180));
    cell("Round", round, card.y + 36, 10, rgba(180, 180, 180));
    cell("Total", total, card.y + 36, 10, rgba(180, 180, 180));
    float y = card.y + 54;
    const float size = std::min(13.0f, layout.rowHeight * 0.72f);
    for (int id : ids) {
        const auto& player = match::player(id);
        if (id == net::self_id()) p.box(card.x + 6, y - 1, card.x + card.w - 6, y + layout.rowHeight - 2, rgba(255, 230, 140, 25));
        const auto name = p.shortened(name_of(id), size, card.w * 0.44f - 20);
        p.text(name, card.x + 12, y + 1, size, player_color(id));
        cell(std::to_string(player.rupeesCollected), loot, y + 1, size, rgba(200, 215, 205));
        cell(std::to_string(player.finds), finds, y + 1, size, rgba(200, 215, 205));
        cell(std::to_string(player.roundPoints), round, y + 1, size, rgba(255, 230, 140));
        cell(std::to_string(player.score), total, y + 1, size, rgba(240, 240, 240));
        y += layout.rowHeight;
    }
}

class HudDlst : public dDlst_base_c {
public:
    void draw() override {
        Painter p;
        const Screen s = screen();
        s_labelCount = 0;
        if (local::blindfolded()) {
            draw_blindfold(p, s);
            draw_feed(p, s);
            return;
        }
        draw_final_markers(p, s);
        draw_taunt_pings(p, s);
        draw_name_tags(p, s);
        draw_treasure(p, s);
        draw_top(p, s);
        draw_role(p, s);
        draw_hider_feedback(p, s);
        draw_feed(p, s);
        if (match::get().phase == Phase::Results) draw_scoreboard(p, s);
        draw_banner(p, s);
    }
};

HudDlst s_dlst;

}  // namespace

void queue() {
    if (net::status() != net::Status::Online || !local::in_world()) return;
    if (ui::is_open() && !local::blindfolded()) return;
    dDlst_list_c& lists = g_dComIfG_gameInfo.drawlist;
    for (dDlst_base_c** it = lists.mp2DXluDrawLists; it < lists.mp2DXluStart; ++it) {
        if (*it == &s_dlst) return;
    }
    dComIfGd_set2DXlu(&s_dlst);
}

}  // namespace hs::hud
