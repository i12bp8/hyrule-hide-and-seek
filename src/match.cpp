#include "match.hpp"

#include "maps.hpp"
#include "net.hpp"
#include "props.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace hs::match {

namespace {

constexpr uint32_t kGatherMs = 25000;
constexpr uint32_t kResultsMs = 12000;
constexpr uint64_t kStateIntervalMs = 100;
constexpr uint64_t kMetaIntervalMs = 30000;
constexpr uint64_t kImmunityMs = 4000;   // after a player loads into a new area
constexpr float kMaxHitDistance = 450.0f;  // sword reach plus network lag
constexpr int kFindPoints = 5;
constexpr int kSurvivePoints = 5;
constexpr int kSecondsPerPoint = 10;
constexpr uint64_t kTauntPointEveryMs = 10000;
constexpr size_t kMaxNotices = 6;

Match s_match;
std::vector<Notice> s_notices;
Hooks s_hooks;
uint64_t s_lastStateSent = 0;
uint64_t s_lastMeta = 0;
uint64_t s_seekStartedAt = 0;  // host clock, for survival points
uint64_t s_lastHitSent[kSlots] = {};
std::mt19937 s_rng{std::random_device{}()};

Player& P(int id) {
    static Player none;
    return id >= 1 && id <= kMaxPlayers ? s_match.players[id] : none;
}

const char* name_of(int id) {
    const char* n = net::member(id).name;
    return n[0] != '\0' ? n : "Someone";
}

void notice(std::string text, int color = -1, bool big = false) {
    Notice n;
    n.text = std::move(text);
    if (color >= 0) {
        const TunicColor& c = color_of(color);
        n.r = c.r;
        n.g = c.g;
        n.b = c.b;
    }
    n.big = big;
    n.at = now_ms();
    s_notices.push_back(std::move(n));
    if (s_notices.size() > kMaxNotices) s_notices.erase(s_notices.begin());
}

void big(std::string text, uint8_t r, uint8_t g, uint8_t b) {
    Notice n;
    n.text = std::move(text);
    n.r = r;
    n.g = g;
    n.b = b;
    n.big = true;
    n.at = now_ms();
    s_notices.push_back(std::move(n));
    if (s_notices.size() > kMaxNotices) s_notices.erase(s_notices.begin());
}

bool host() {
    return net::is_host();
}

uint8_t self() {
    return net::self_id();
}

// ---- host: sending ---------------------------------------------------------------------------

void apply(uint8_t from, const std::vector<uint8_t>& bytes);

// Host announcement: to everyone else, and applied here too.
void announce(const Writer& w) {
    net::send(net::kToEveryone, w.bytes());
    apply(self(), w.bytes());
}

Writer settings_msg() {
    Writer w(MSG_SETTINGS);
    s_match.settings.write(w);
    return w;
}

Writer roster_msg() {
    Writer w(MSG_ROSTER);
    uint8_t n = 0;
    for (int id = 1; id <= kMaxPlayers; ++id) n += P(id).present ? 1 : 0;
    w.u8(n);
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const Player& p = P(id);
        if (!p.present) continue;
        w.u8(static_cast<uint8_t>(id));
        w.u8(static_cast<uint8_t>(p.role));
        w.u8(p.color);
        w.u16(p.score);
        w.u16(p.roundPoints);
        w.u8(static_cast<uint8_t>((p.found ? 1 : 0) | (p.ready ? 2 : 0)));
        w.u8(p.hunterRounds);
    }
    return w;
}

Writer round_msg() {
    Writer w(MSG_ROUND);
    w.u32(s_match.round);
    w.u8(static_cast<uint8_t>(s_match.settings.mode));
    w.u8(s_match.map);
    w.u16(s_match.settings.hideSecs);
    w.u16(s_match.settings.seekSecs);
    return w;
}

Writer phase_msg() {
    Writer w(MSG_PHASE);
    w.u32(s_match.round);
    w.u8(static_cast<uint8_t>(s_match.phase));
    w.u32(ms_left());
    return w;
}

void send_meta() {
    const Settings& s = s_match.settings;
    std::string label = std::string(name_of(self())) + "'s room";
    net::send_meta(s.isPublic, label, static_cast<int>(s.mode),
        s.map == kRandomMap ? 255 : s.map, static_cast<int>(s_match.phase));
    s_lastMeta = now_ms();
}

void go_phase(Phase phase, uint32_t ms) {
    s_match.phase = phase;
    s_match.phaseEnd = now_ms() + ms;
    if (phase == Phase::Seek) s_seekStartedAt = now_ms();
    announce(phase_msg());
    send_meta();
}

void assign_color(int id) {
    Player& p = P(id);
    bool used[kMaxPlayers] = {};
    for (int other = 1; other <= kMaxPlayers; ++other) {
        if (other != id && P(other).present) used[P(other).color] = true;
    }
    if (p.wantColor < kMaxPlayers && !used[p.wantColor]) {
        p.color = p.wantColor;
        return;
    }
    if (!used[p.color] && p.color < kMaxPlayers) return;
    for (uint8_t c = 0; c < kMaxPlayers; ++c) {
        if (!used[c]) {
            p.color = c;
            return;
        }
    }
}

int survival_points(uint64_t until) {
    if (s_seekStartedAt == 0 || until <= s_seekStartedAt) return 0;
    return static_cast<int>((until - s_seekStartedAt) / 1000 / kSecondsPerPoint);
}

void give(int id, int points) {
    Player& p = P(id);
    p.roundPoints = static_cast<uint16_t>(std::min(65535, p.roundPoints + points));
    p.score = static_cast<uint16_t>(std::min(65535, p.score + points));
}

void finish(int winner) {
    const uint64_t now = now_ms();
    for (int id = 1; id <= kMaxPlayers; ++id) {
        Player& p = P(id);
        if (p.present && p.role == Role::Hider && !p.found) {
            give(id, survival_points(now) + (winner == 0 ? kSurvivePoints : 0));
        }
    }
    Writer w(MSG_RESULTS);
    w.u32(s_match.round);
    w.u8(static_cast<uint8_t>(winner));
    announce(w);
    announce(roster_msg());
    go_phase(Phase::Results, kResultsMs);
}

void found(int target, int by) {
    Player& t = P(target);
    t.found = true;
    t.role = s_match.settings.foundJoinHunters ? Role::Hunter : Role::Spectator;
    give(target, survival_points(now_ms()));
    give(by, kFindPoints);
    Writer w(MSG_FOUND);
    w.u32(s_match.round);
    w.u8(static_cast<uint8_t>(target));
    w.u8(static_cast<uint8_t>(by));
    announce(w);
    announce(roster_msg());
}

float distance(const PlayerState& a, const PlayerState& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void host_hit(int attacker, int target) {
    if (s_match.phase != Phase::Seek) return;
    Player& a = P(attacker);
    Player& t = P(target);
    if (!a.present || !t.present || a.role != Role::Hunter || t.role != Role::Hider || t.found) return;
    if (!a.hasState || !t.hasState) return;
    if (std::strcmp(a.state.stage, t.state.stage) != 0) return;
    if (now_ms() - t.stageChangedAt < kImmunityMs) return;
    const float d = distance(a.state, t.state);
    if (d > kMaxHitDistance) {
        mods::log::info("ignored hit {} -> {}: {:.0f} apart", attacker, target, d);
        return;
    }
    found(target, attacker);
}

void host_update() {
    const uint64_t now = now_ms();
    switch (s_match.phase) {
    case Phase::Gather: {
        bool all = true;
        for (int id = 1; id <= kMaxPlayers; ++id) {
            const Player& p = P(id);
            if (p.present && p.role != Role::Spectator && !p.ready) all = false;
        }
        if (all || now >= s_match.phaseEnd) go_phase(Phase::Hide, s_match.settings.hideSecs * 1000u);
        break;
    }
    case Phase::Hide:
        if (now >= s_match.phaseEnd) go_phase(Phase::Seek, s_match.settings.seekSecs * 1000u);
        break;
    case Phase::Seek:
        if (count_role(Role::Hider) == 0) {
            finish(1);
        } else if (count_role(Role::Hunter) == 0) {
            finish(0);
        } else if (now >= s_match.phaseEnd) {
            finish(0);
        }
        break;
    case Phase::Results:
        if (now >= s_match.phaseEnd) {
            std::string why;
            if (s_match.settings.autoNext && can_start(&why)) {
                start_round();
            } else {
                go_phase(Phase::Lobby, 0);
                for (int id = 1; id <= kMaxPlayers; ++id) {
                    if (P(id).present) P(id).role = Role::None;
                }
                announce(roster_msg());
            }
        }
        break;
    case Phase::Lobby: break;
    }
    if (s_match.settings.isPublic && now - s_lastMeta > kMetaIntervalMs) send_meta();
}

// ---- everyone: receiving ---------------------------------------------------------------------

void apply(uint8_t from, const std::vector<uint8_t>& bytes) {
    on_message(from, bytes.data(), bytes.size());
}

void handle_roster(Reader& r) {
    const uint8_t n = r.u8();
    bool listed[kSlots] = {};
    for (int i = 0; i < n && r.ok(); ++i) {
        const uint8_t id = r.u8();
        const Role role = static_cast<Role>(r.u8());
        const uint8_t color = r.u8();
        const uint16_t score = r.u16();
        const uint16_t roundPoints = r.u16();
        const uint8_t flags = r.u8();
        const uint8_t hunterRounds = r.u8();
        if (!r.ok() || id < 1 || id > kMaxPlayers) break;
        Player& p = P(id);
        listed[id] = true;
        p.present = true;
        p.role = role;
        p.color = color < kMaxPlayers ? color : 0;
        p.score = score;
        p.roundPoints = roundPoints;
        p.found = (flags & 1) != 0;
        p.ready = (flags & 2) != 0;
        p.hunterRounds = hunterRounds;
    }
    (void)listed;
}

void handle_state(uint8_t from, Reader& r) {
    Player& p = P(from);
    PlayerState s;
    s.read(r);
    if (!r.ok()) return;
    if (!p.hasState || std::strcmp(p.state.stage, s.stage) != 0) p.stageChangedAt = now_ms();
    p.state = s;
    p.hasState = true;
    p.stateAt = now_ms();
}

}  // namespace

// ---- Settings ---------------------------------------------------------------------------------

void Settings::write(Writer& w) const {
    w.u8(static_cast<uint8_t>(mode));
    w.u8(map);
    w.u16(hideSecs);
    w.u16(seekSecs);
    w.u8(hunters);
    w.u8(static_cast<uint8_t>((foundJoinHunters ? 1 : 0) | (missPenalty ? 2 : 0) |
                              (autoTaunt ? 4 : 0) | (autoNext ? 8 : 0) | (isPublic ? 16 : 0)));
}

void Settings::read(Reader& r) {
    const uint8_t m = r.u8();
    mode = m < static_cast<uint8_t>(Mode::Count) ? static_cast<Mode>(m) : Mode::PropHunt;
    map = r.u8();
    hideSecs = std::clamp<uint16_t>(r.u16(), 10, 600);
    seekSecs = std::clamp<uint16_t>(r.u16(), 30, 1800);
    hunters = r.u8();
    const uint8_t f = r.u8();
    foundJoinHunters = f & 1;
    missPenalty = f & 2;
    autoTaunt = f & 4;
    autoNext = f & 8;
    isPublic = f & 16;
}

// ---- queries ---------------------------------------------------------------------------------

const Match& get() {
    return s_match;
}

const Player& player(int id) {
    return P(id);
}

Role my_role() {
    return P(self()).role;
}

bool in_round() {
    return s_match.phase == Phase::Gather || s_match.phase == Phase::Hide ||
           s_match.phase == Phase::Seek;
}

uint32_t ms_left() {
    const uint64_t now = now_ms();
    return s_match.phaseEnd > now ? static_cast<uint32_t>(s_match.phaseEnd - now) : 0;
}

int count_role(Role role) {
    int n = 0;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const Player& p = P(id);
        if (p.present && p.role == role && !(role == Role::Hider && p.found)) ++n;
    }
    return n;
}

int hiders_left() {
    return count_role(Role::Hider);
}

const std::vector<Notice>& notices() {
    return s_notices;
}

// ---- actions ---------------------------------------------------------------------------------

void set_settings(const Settings& s) {
    if (!host()) return;
    announce([&] {
        Writer w(MSG_SETTINGS);
        s.write(w);
        return w;
    }());
    send_meta();
}

bool can_start(std::string* why) {
    if (!host()) {
        if (why) *why = "Only the host can start a round.";
        return false;
    }
    int n = 0;
    for (int id = 1; id <= kMaxPlayers; ++id) n += P(id).present ? 1 : 0;
    if (n < 2) {
        if (why) *why = "Waiting for at least one more player.";
        return false;
    }
    return true;
}

void start_round() {
    if (!can_start(nullptr)) return;
    std::vector<int> ids;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (P(id).present) ids.push_back(id);
    }
    const int n = static_cast<int>(ids.size());
    int hunters = s_match.settings.hunters != 0 ? s_match.settings.hunters
                                                : static_cast<int>(std::lround(n / 4.0));
    hunters = std::clamp(hunters, 1, n - 1);

    // Fewest turns as hunter first, ties broken at random.
    std::shuffle(ids.begin(), ids.end(), s_rng);
    std::stable_sort(ids.begin(), ids.end(),
        [](int a, int b) { return P(a).hunterRounds < P(b).hunterRounds; });
    for (int i = 0; i < n; ++i) {
        Player& p = P(ids[i]);
        p.role = i < hunters ? Role::Hunter : Role::Hider;
        if (p.role == Role::Hunter) ++p.hunterRounds;
        p.found = false;
        p.ready = false;
        p.roundPoints = 0;
    }

    s_match.round += 1;
    s_match.map = s_match.settings.map == kRandomMap ? static_cast<uint8_t>(random_map(s_match.map))
                                                     : s_match.settings.map;
    s_match.winner = -1;
    s_seekStartedAt = 0;
    announce(roster_msg());
    announce(round_msg());
    go_phase(Phase::Gather, kGatherMs);
}

void end_round() {
    if (!host() || !in_round()) return;
    finish(0);
}

void reset_scores() {
    if (!host()) return;
    for (Player& p : s_match.players) {
        p.score = 0;
        p.roundPoints = 0;
        p.hunterRounds = 0;
    }
    announce(roster_msg());
}

void set_wanted_color(uint8_t color) {
    if (net::status() != net::Status::Online) return;
    Writer w(MSG_HELLO);
    w.u8(color);
    net::send(net::kToHost, w.bytes());
    if (host()) apply(self(), w.bytes());
}

void set_local_state(const PlayerState& s) {
    if (net::status() != net::Status::Online) return;
    Player& me = P(self());
    if (!me.hasState || std::strcmp(me.state.stage, s.stage) != 0) me.stageChangedAt = now_ms();
    me.state = s;
    me.hasState = true;
    me.stateAt = now_ms();
    const uint64_t now = now_ms();
    if (now - s_lastStateSent < kStateIntervalMs) return;
    s_lastStateSent = now;
    Writer w(MSG_STATE);
    s.write(w);
    net::send(net::kToEveryone, w.bytes());
}

void report_ready() {
    Writer w(MSG_READY);
    w.u32(s_match.round);
    if (host()) {
        apply(self(), w.bytes());
    } else {
        net::send(net::kToHost, w.bytes());
    }
}

void report_hit(uint8_t target) {
    if (target < 1 || target > kMaxPlayers) return;
    const uint64_t now = now_ms();
    if (now - s_lastHitSent[target] < 500) return;
    s_lastHitSent[target] = now;
    if (host()) {
        host_hit(self(), target);
        return;
    }
    Writer w(MSG_HIT);
    w.u32(s_match.round);
    w.u8(target);
    net::send(net::kToHost, w.bytes());
}

void send_taunt(uint8_t sound) {
    Writer w(MSG_TAUNT);
    w.u8(sound);
    net::send(net::kToEveryone, w.bytes());
    if (host()) {
        Player& me = P(self());
        const uint64_t now = now_ms();
        if (s_match.phase == Phase::Seek && me.role == Role::Hider &&
            now - me.lastTauntAt > kTauntPointEveryMs) {
            me.lastTauntAt = now;
            give(self(), 1);
            announce(roster_msg());
        }
    }
}

// ---- net wiring ------------------------------------------------------------------------------

void on_welcome() {
    s_match = Match{};
    s_notices.clear();
    for (int id = 1; id <= kMaxPlayers; ++id) {
        P(id).present = net::member(id).present;
        P(id).color = static_cast<uint8_t>((id - 1) % kMaxPlayers);
    }
    if (host()) {
        assign_color(self());
        announce(settings_msg());
        announce(roster_msg());
        notice("Room " + net::room_code() + " is open. Share the code!", -1, false);
    } else {
        notice("Joined room " + net::room_code(), -1, false);
    }
}

void on_joined(uint8_t id) {
    Player& p = P(id);
    p = Player{};
    p.present = true;
    p.color = static_cast<uint8_t>((id - 1) % kMaxPlayers);
    notice(std::string(name_of(id)) + " joined", -1, false);
    if (!host()) return;
    assign_color(id);
    if (in_round()) p.role = s_match.settings.foundJoinHunters ? Role::Hunter : Role::Spectator;
    const Writer settings = settings_msg();
    net::send(id, settings.bytes());
    announce(roster_msg());
    if (in_round() || s_match.phase == Phase::Results) {
        net::send(id, round_msg().bytes());
        net::send(id, phase_msg().bytes());
    }
    send_meta();
}

void on_left(uint8_t id) {
    notice(std::string(name_of(id)) + " left", -1, false);
    P(id).present = false;
    P(id).hasState = false;
    if (host()) {
        announce(roster_msg());
        send_meta();
    }
}

void on_host_changed(uint8_t id) {
    notice(std::string(name_of(id)) + " is the host now", -1, false);
    if (host()) {
        // Carry on with the round we already know about.
        if (s_match.phase == Phase::Seek && s_seekStartedAt == 0) {
            s_seekStartedAt = now_ms() - (s_match.settings.seekSecs * 1000u - ms_left());
        }
        announce(roster_msg());
        send_meta();
    }
}

void on_disconnected() {
    s_match.phase = Phase::Lobby;
    for (Player& p : s_match.players) p = Player{};
}

void on_message(uint8_t from, const uint8_t* data, size_t size) {
    if (size < 1 || from < 1 || from > kMaxPlayers) return;
    Reader r(data + 1, size - 1);
    const bool fromHost = from == net::host_id();
    switch (data[0]) {
    case MSG_STATE: handle_state(from, r); break;

    case MSG_HELLO: {
        const uint8_t want = r.u8();
        if (!r.ok() || !host()) break;
        P(from).wantColor = want;
        assign_color(from);
        announce(roster_msg());
        break;
    }

    case MSG_SETTINGS:
        if (!fromHost) break;
        s_match.settings.read(r);
        break;

    case MSG_ROSTER:
        if (fromHost) handle_roster(r);
        break;

    case MSG_ROUND: {
        if (!fromHost) break;
        const uint32_t round = r.u32();
        const uint8_t mode = r.u8();
        const uint8_t map = r.u8();
        const uint16_t hide = r.u16();
        const uint16_t seek = r.u16();
        if (!r.ok()) break;
        s_match.round = round;
        s_match.settings.mode = mode < static_cast<uint8_t>(Mode::Count) ? static_cast<Mode>(mode)
                                                                        : Mode::PropHunt;
        s_match.map = map;
        s_match.settings.hideSecs = hide;
        s_match.settings.seekSecs = seek;
        s_match.winner = -1;
        const char* role = my_role() == Role::Hunter ? "You are a HUNTER" : "You are a PROP";
        if (s_match.settings.mode == Mode::HideAndSeek && my_role() == Role::Hider) {
            role = "You are HIDING";
        }
        big(std::string("Round ") + std::to_string(round) + ": " + map_info(map).name, 255, 230, 140);
        notice(role, P(self()).color, false);
        if (s_hooks.roundStarted) s_hooks.roundStarted();
        break;
    }

    case MSG_PHASE: {
        if (!fromHost) break;
        const uint32_t round = r.u32();
        const Phase phase = static_cast<Phase>(r.u8());
        const uint32_t left = r.u32();
        if (!r.ok()) break;
        const Phase before = s_match.phase;
        s_match.round = round;
        s_match.phase = phase;
        s_match.phaseEnd = now_ms() + left;
        if (phase == before) break;
        if (phase == Phase::Hide) {
            if (my_role() == Role::Hunter) {
                big("Wait for it...", 255, 120, 90);
            } else {
                big("HIDE!", 120, 230, 120);
            }
        } else if (phase == Phase::Seek) {
            big(my_role() == Role::Hunter ? "HUNT THEM DOWN!" : "The hunters are coming!", 255, 90, 70);
        }
        break;
    }

    case MSG_FOUND: {
        if (!fromHost) break;
        r.u32();
        const uint8_t target = r.u8();
        const uint8_t by = r.u8();
        if (!r.ok()) break;
        Player& t = P(target);
        t.found = true;
        std::string what = s_match.settings.mode == Mode::PropHunt && t.hasState
                               ? std::string(" (") + prop_info(t.state.prop).name + ")"
                               : std::string();
        notice(std::string(name_of(by)) + " found " + name_of(target) + what, P(by).color, false);
        if (target == self()) {
            big("You were found!", 255, 120, 90);
            if (s_hooks.foundMe) s_hooks.foundMe(by);
        } else if (by == self()) {
            big(std::string("Found ") + name_of(target) + "!", 255, 230, 120);
        }
        break;
    }

    case MSG_RESULTS: {
        if (!fromHost) break;
        r.u32();
        const uint8_t winner = r.u8();
        if (!r.ok()) break;
        s_match.winner = winner;
        if (winner == 1) {
            big("HUNTERS WIN!", 255, 110, 90);
        } else {
            big(s_match.settings.mode == Mode::PropHunt ? "PROPS WIN!" : "HIDERS WIN!", 120, 230, 120);
        }
        break;
    }

    case MSG_READY: {
        const uint32_t round = r.u32();
        if (!r.ok() || !host() || round != s_match.round) break;
        P(from).ready = true;
        break;
    }

    case MSG_HIT: {
        const uint32_t round = r.u32();
        const uint8_t target = r.u8();
        if (!r.ok() || !host() || round != s_match.round) break;
        host_hit(from, target);
        break;
    }

    case MSG_TAUNT: {
        const uint8_t sound = r.u8();
        if (!r.ok()) break;
        if (s_hooks.taunt) s_hooks.taunt(from, sound);
        Player& p = P(from);
        const uint64_t now = now_ms();
        if (host() && s_match.phase == Phase::Seek && p.role == Role::Hider && !p.found &&
            now - p.lastTauntAt > kTauntPointEveryMs) {
            p.lastTauntAt = now;
            give(from, 1);
            announce(roster_msg());
        }
        break;
    }

    default: break;
    }
}

void update() {
    if (net::status() != net::Status::Online) return;
    if (host()) host_update();
    // Drop notices after a while so the feed stays short.
    const uint64_t now = now_ms();
    s_notices.erase(std::remove_if(s_notices.begin(), s_notices.end(),
                        [now](const Notice& n) { return now - n.at > (n.big ? 3500u : 7000u); }),
        s_notices.end());
}

void set_hooks(const Hooks& hooks) {
    s_hooks = hooks;
}

}  // namespace hs::match
