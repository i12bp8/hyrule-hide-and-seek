// Rules test: drives src/match.cpp through whole rounds with a fake network and a hand-cranked
// clock. Build and run with tests/run.sh (no game or Dusklight needed).

#include "fake_net.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "protocol.hpp"
#include "props.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace hs;
using hs::net::g_fake;

static uint64_t s_now = 1'000'000;
uint64_t hs::test_now_ms() {
    return s_now;
}

static int s_failed = 0;
static int s_checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++s_checks;                                                                                \
        if (!(cond)) {                                                                             \
            ++s_failed;                                                                            \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                           \
        }                                                                                          \
    } while (0)

static void advance(uint64_t ms) {
    // Step in frames so the host's update() sees every boundary.
    for (uint64_t t = 0; t < ms; t += 33) {
        s_now += 33;
        match::update();
    }
}

// Deliver everything the "host" sent to everyone as if the relay echoed it to a client. Here we
// only need to inspect it; the host already applied its own announcements.
static int count_sent(uint8_t type) {
    int n = 0;
    for (const auto& s : g_fake.sent) n += !s.bytes.empty() && s.bytes[0] == type ? 1 : 0;
    return n;
}

static std::vector<uint8_t> state_msg(const char* stage, float x, float y, float z, uint8_t flags = STATE_IN_WORLD) {
    PlayerState s;
    s.flags = flags;
    copy_str(s.stage, stage);
    s.x = x;
    s.y = y;
    s.z = z;
    Writer w(MSG_STATE);
    s.write(w);
    return w.bytes();
}

static void deliver(uint8_t from, const std::vector<uint8_t>& bytes) {
    match::on_message(from, bytes.data(), bytes.size());
}

static std::vector<uint8_t> ready_msg(uint32_t round) {
    Writer w(MSG_READY);
    w.u32(round);
    return w.bytes();
}

static std::vector<uint8_t> hit_msg(uint32_t round, uint8_t target) {
    Writer w(MSG_HIT);
    w.u32(round);
    w.u8(target);
    return w.bytes();
}

static void reset_net(int players) {
    g_fake = net::FakeNet{};
    for (int id = 1; id <= players; ++id) g_fake.add(id, ("P" + std::to_string(id)).c_str());
}

static void host_room(int players) {
    reset_net(players);
    match::on_welcome();
    match::Settings s;
    s.map = 0;
    s.hideSecs = 20;
    s.seekSecs = 60;
    s.autoNext = false;
    match::set_settings(s);
}

static int hunter_id() {
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (match::player(id).present && match::player(id).role == Role::Hunter) return id;
    }
    return 0;
}

static int hider_id() {
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (match::player(id).present && match::player(id).role == Role::Hider) return id;
    }
    return 0;
}

// Everyone reports their position on the map, then everyone is ready.
static void everyone_ready(int players, const char* stage) {
    for (int id = 1; id <= players; ++id) {
        const auto st = state_msg(stage, id * 1000.0f, 0.0f, 0.0f);
        if (id == g_fake.self) {
            PlayerState s;
            s.flags = STATE_IN_WORLD;
            copy_str(s.stage, stage);
            s.x = id * 1000.0f;
            match::set_local_state(s);
            match::report_ready();
        } else {
            deliver(static_cast<uint8_t>(id), st);
            deliver(static_cast<uint8_t>(id), ready_msg(match::get().round));
        }
    }
}

static void test_protocol_roundtrip() {
    std::printf("protocol round trip\n");
    PlayerState a;
    a.flags = STATE_IN_WORLD | STATE_DISGUISED;
    copy_str(a.stage, "F_SP103");
    a.room = 3;
    a.x = 1.5f;
    a.y = -2.25f;
    a.z = 3000.0f;
    a.yaw = -1234;
    a.prop = 9;
    a.propYaw = 4321;
    a.under[1] = {0x26A, 12.5f, 128};
    a.upper[2] = {0x100, 3.0f, 255};
    Writer w(MSG_STATE);
    a.write(w);
    CHECK(w.bytes().size() == 70);  // type + 69 bytes of state
    Reader r(w.bytes().data() + 1, w.bytes().size() - 1);
    PlayerState b;
    b.read(r);
    CHECK(r.ok());
    CHECK(std::strcmp(b.stage, "F_SP103") == 0);
    CHECK(b.room == 3 && b.x == 1.5f && b.y == -2.25f && b.z == 3000.0f);
    CHECK(b.yaw == -1234 && b.prop == 9 && b.propYaw == 4321);
    CHECK(b.under[1].idx == 0x26A && b.under[1].frame == 12.5f && b.under[1].ratio == 128);
    CHECK(b.upper[2].idx == 0x100 && b.upper[2].ratio == 255);
    CHECK(b.flags == (STATE_IN_WORLD | STATE_DISGUISED));

    // A truncated message is rejected, not read past its end.
    Reader shortReader(w.bytes().data() + 1, 20);
    PlayerState c;
    c.read(shortReader);
    CHECK(!shortReader.ok());

    match::Settings s;
    s.mode = Mode::HideAndSeek;
    s.map = kRandomMap;
    s.hideSecs = 5;      // clamps to 10
    s.seekSecs = 60000;  // clamps to 1800
    s.hunters = 2;
    s.missPenalty = false;
    s.isPublic = true;
    Writer sw(MSG_SETTINGS);
    s.write(sw);
    Reader sr(sw.bytes().data() + 1, sw.bytes().size() - 1);
    match::Settings t;
    t.read(sr);
    CHECK(sr.ok());
    CHECK(t.mode == Mode::HideAndSeek && t.map == kRandomMap && t.hunters == 2);
    CHECK(t.hideSecs == 10 && t.seekSecs == 1800);
    CHECK(!t.missPenalty && t.isPublic && t.foundJoinHunters && t.autoTaunt && t.autoNext);
}

static void test_full_round_two_players() {
    std::printf("two players, hunter finds the prop\n");
    host_room(2);
    CHECK(count_sent(MSG_SETTINGS) >= 1);
    CHECK(count_sent(MSG_ROSTER) >= 1);
    CHECK(match::player(1).color != match::player(2).color);

    std::string why;
    CHECK(match::can_start(&why));
    match::start_round();
    CHECK(match::get().phase == Phase::Gather);
    CHECK(match::get().round == 1);
    CHECK(match::count_role(Role::Hunter) == 1);
    CHECK(match::count_role(Role::Hider) == 1);
    const char* stage = map_info(match::get().map).stage;

    everyone_ready(2, stage);
    advance(100);
    CHECK(match::get().phase == Phase::Hide);

    advance(19'000);
    CHECK(match::get().phase == Phase::Hide);
    advance(2'000);
    CHECK(match::get().phase == Phase::Seek);

    const int hunter = hunter_id();
    const int hider = hider_id();
    CHECK(hunter != 0 && hider != 0 && hunter != hider);

    // Put them next to each other.
    advance(30'000);  // the prop survives 30 s: 3 points
    const auto near = [&](int id, float x) {
        if (id == g_fake.self) {
            PlayerState s;
            s.flags = STATE_IN_WORLD;
            copy_str(s.stage, stage);
            s.x = x;
            match::set_local_state(s);
        } else {
            deliver(static_cast<uint8_t>(id), state_msg(stage, x, 0.0f, 0.0f));
        }
    };
    near(hunter, 0.0f);
    near(hider, 5000.0f);
    // Too far away: ignored.
    if (hunter == g_fake.self) match::report_hit(static_cast<uint8_t>(hider));
    else deliver(static_cast<uint8_t>(hunter), hit_msg(1, static_cast<uint8_t>(hider)));
    CHECK(!match::player(hider).found);

    near(hider, 100.0f);
    advance(600);  // the hunter's own reports are rate limited per target
    if (hunter == g_fake.self) match::report_hit(static_cast<uint8_t>(hider));
    else deliver(static_cast<uint8_t>(hunter), hit_msg(1, static_cast<uint8_t>(hider)));
    CHECK(match::player(hider).found);
    CHECK(count_sent(MSG_FOUND) == 1);
    CHECK(match::player(hunter).roundPoints == 5);
    CHECK(match::player(hider).roundPoints == 3);

    advance(100);
    CHECK(match::get().phase == Phase::Results);
    CHECK(match::get().winner == 1);
    CHECK(count_sent(MSG_RESULTS) == 1);

    advance(13'000);
    CHECK(match::get().phase == Phase::Lobby);  // autoNext is off
    CHECK(match::player(1).role == Role::None);
}

static void test_props_win_on_time_and_rotation() {
    std::printf("props survive the timer; next round rotates the hunter\n");
    host_room(4);
    match::Settings s = match::get().settings;
    s.autoNext = true;
    match::set_settings(s);
    match::start_round();
    CHECK(match::count_role(Role::Hunter) == 1);  // four players: one hunter
    const int firstHunter = hunter_id();
    everyone_ready(4, map_info(match::get().map).stage);
    advance(21'000);
    CHECK(match::get().phase == Phase::Seek);
    advance(61'000);
    CHECK(match::get().phase == Phase::Results);
    CHECK(match::get().winner == 0);
    for (int id = 1; id <= 4; ++id) {
        if (id == firstHunter) CHECK(match::player(id).roundPoints == 0);
        else CHECK(match::player(id).roundPoints == 6 + 5);  // 60 s hidden + survived
    }
    advance(13'000);
    CHECK(match::get().round == 2);
    CHECK(match::get().phase == Phase::Gather);
    CHECK(hunter_id() != firstHunter);
}

static void test_gather_timeout() {
    std::printf("a player who never loads doesn't hold up the round\n");
    host_room(3);
    match::start_round();
    // Only the host reports in.
    PlayerState me;
    me.flags = STATE_IN_WORLD;
    copy_str(me.stage, map_info(match::get().map).stage);
    match::set_local_state(me);
    match::report_ready();
    advance(10'000);
    CHECK(match::get().phase == Phase::Gather);
    advance(16'000);
    CHECK(match::get().phase == Phase::Hide);
}

static void test_hit_rules() {
    std::printf("only hunters hit, only during the hunt, not right after a loading zone\n");
    host_room(3);
    match::Settings s = match::get().settings;
    s.hunters = 1;
    match::set_settings(s);
    match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(3, stage);
    advance(100);
    CHECK(match::get().phase == Phase::Hide);
    const int hunter = hunter_id();
    int hiders[2] = {0, 0};
    int n = 0;
    for (int id = 1; id <= 3; ++id) {
        if (match::player(id).role == Role::Hider) hiders[n++] = id;
    }
    // Everyone stands on the same spot.
    for (int id = 1; id <= 3; ++id) {
        if (id == g_fake.self) {
            PlayerState p;
            p.flags = STATE_IN_WORLD;
            copy_str(p.stage, stage);
            match::set_local_state(p);
        } else {
            deliver(static_cast<uint8_t>(id), state_msg(stage, 0, 0, 0));
        }
    }
    const auto hit = [&](int from, int target) {
        if (from == g_fake.self) match::report_hit(static_cast<uint8_t>(target));
        else deliver(static_cast<uint8_t>(from), hit_msg(match::get().round, static_cast<uint8_t>(target)));
    };
    hit(hunter, hiders[0]);  // still hiding time
    CHECK(!match::player(hiders[0]).found);
    advance(21'000);
    CHECK(match::get().phase == Phase::Seek);
    hit(hiders[1], hiders[0]);  // a prop can't find a prop
    CHECK(!match::player(hiders[0]).found);

    // hiders[0] just walked through a door into another area and back: immune for a moment.
    if (hiders[0] != g_fake.self) {
        deliver(static_cast<uint8_t>(hiders[0]), state_msg("R_SP01", 0, 0, 0));
        deliver(static_cast<uint8_t>(hiders[0]), state_msg(stage, 0, 0, 0));
        advance(600);
        hit(hunter, hiders[0]);
        CHECK(!match::player(hiders[0]).found);
        advance(4'000);
    }
    hit(hunter, hiders[0]);
    CHECK(match::player(hiders[0]).found);
    CHECK(match::player(hiders[0]).role == Role::Hunter);  // found props join the hunters
    // The newly made hunter can find the last prop.
    advance(600);
    hit(hiders[0], hiders[1]);
    CHECK(match::player(hiders[1]).found);
    advance(100);
    CHECK(match::get().phase == Phase::Results);
    CHECK(match::get().winner == 1);
}

static void test_late_join_and_leave() {
    std::printf("late joiners get the round; the last prop leaving ends it\n");
    host_room(2);
    match::start_round();
    everyone_ready(2, map_info(match::get().map).stage);
    advance(21'000);
    CHECK(match::get().phase == Phase::Seek);

    g_fake.add(3, "Late");
    g_fake.sent.clear();
    match::on_joined(3);
    CHECK(match::player(3).role == Role::Hunter);
    bool gotRound = false, gotPhase = false, gotSettings = false;
    for (const auto& m : g_fake.sent) {
        if (m.to != 3) continue;
        gotRound |= m.bytes[0] == MSG_ROUND;
        gotPhase |= m.bytes[0] == MSG_PHASE;
        gotSettings |= m.bytes[0] == MSG_SETTINGS;
    }
    CHECK(gotRound && gotPhase && gotSettings);

    const int hider = hider_id();
    if (hider != g_fake.self) {
        g_fake.remove(hider);
        match::on_left(static_cast<uint8_t>(hider));
        advance(100);
        CHECK(match::get().phase == Phase::Results);
        CHECK(match::get().winner == 1);
    }
}

static void test_client_and_host_migration() {
    std::printf("a client follows the host, then takes over when the host leaves\n");
    reset_net(3);
    g_fake.self = 2;
    g_fake.host = 1;
    match::on_welcome();
    CHECK(!net::is_host());
    CHECK(!match::can_start(nullptr));

    // The host's roster and round, as they'd arrive over the relay.
    Writer roster(MSG_ROSTER);
    roster.u8(3);
    const Role roles[4] = {Role::None, Role::Hunter, Role::Hider, Role::Hider};
    for (uint8_t id = 1; id <= 3; ++id) {
        roster.u8(id);
        roster.u8(static_cast<uint8_t>(roles[id]));
        roster.u8(id);
        roster.u16(0);
        roster.u16(0);
        roster.u8(0);
        roster.u8(id == 1 ? 1 : 0);
    }
    deliver(1, roster.bytes());
    Writer round(MSG_ROUND);
    round.u32(7);
    round.u8(0);
    round.u8(2);
    round.u16(30);
    round.u16(120);
    deliver(1, round.bytes());
    Writer phase(MSG_PHASE);
    phase.u32(7);
    phase.u8(static_cast<uint8_t>(Phase::Seek));
    phase.u32(90'000);
    deliver(1, phase.bytes());
    CHECK(match::get().round == 7);
    CHECK(match::get().map == 2);
    CHECK(match::get().phase == Phase::Seek);
    CHECK(match::my_role() == Role::Hider);
    CHECK(match::ms_left() == 90'000);

    // A roster from someone who isn't the host is ignored.
    Writer fake(MSG_ROSTER);
    fake.u8(1);
    fake.u8(2);
    fake.u8(static_cast<uint8_t>(Role::Hunter));
    fake.u8(0);
    fake.u16(999);
    fake.u16(0);
    fake.u8(0);
    fake.u8(0);
    deliver(3, fake.bytes());
    CHECK(match::my_role() == Role::Hider);

    // Host leaves; we're next in line.
    g_fake.remove(1);
    match::on_left(1);
    g_fake.host = 2;
    match::on_host_changed(2);
    CHECK(net::is_host());
    CHECK(match::get().phase == Phase::Seek);
    advance(100);
    // No hunters remain (the only one left), so the props win right away.
    CHECK(match::get().phase == Phase::Results);
    CHECK(match::get().winner == 0);
    // ...and the new host carries on with the next round by itself.
    advance(13'000);
    CHECK(match::get().round == 8);
    CHECK(match::get().phase == Phase::Gather);
}

static void test_colors_unique() {
    std::printf("every player gets their own colour\n");
    reset_net(16);
    match::on_welcome();
    for (int id = 2; id <= 16; ++id) {
        Writer hello(MSG_HELLO);
        hello.u8(5);  // everyone wants Orange
        deliver(static_cast<uint8_t>(id), hello.bytes());
    }
    bool used[kMaxPlayers] = {};
    bool unique = true;
    for (int id = 1; id <= 16; ++id) {
        const uint8_t c = match::player(id).color;
        if (c >= kMaxPlayers || used[c]) unique = false;
        if (c < kMaxPlayers) used[c] = true;
    }
    CHECK(unique);
    int orange = 0;
    for (int id = 1; id <= 16; ++id) orange += match::player(id).color == 5 ? 1 : 0;
    CHECK(orange == 1);
}

static void test_maps() {
    std::printf("maps\n");
    CHECK(map_count() >= 10);
    for (int i = 0; i < 200; ++i) {
        const int m = random_map(3);
        CHECK(m >= 0 && m < map_count() && m != 3);
    }
}

static void test_props() {
    std::printf("prop catalogue\n");
    CHECK(prop_count() == 21);
    for (int i = 0; i < prop_count(); ++i) {
        const PropInfo& prop = prop_info(i);
        CHECK(prop.name != nullptr && prop.arc != nullptr);
        CHECK(prop.bmd != nullptr || prop.bmdIndex >= 0);
        CHECK(prop.radius > 0.0f && prop.height > 0.0f && prop.scale > 0.0f);
    }
    CHECK(std::strcmp(prop_info(15).name, "Sign") == 0);
    CHECK(std::strcmp(prop_info(20).name, "Gravestone") == 0);
    CHECK(prop_for_carry_type(3) == 10);   // cannonball
    CHECK(prop_for_carry_type(6) == 11);   // Deku nut
    CHECK(prop_for_carry_type(10) == 12);  // big blue pot
    CHECK(prop_for_carry_type(12) == 13);  // small Twilight pot
    CHECK(prop_for_carry_type(13) == 14);  // big Twilight pot
    CHECK(prop_for_carry_type(99) == -1);
    for (int i = 0; i < 200; ++i) {
        const int prop = random_prop();
        CHECK(prop >= 0 && prop < prop_count());
    }
}

int main() {
    test_protocol_roundtrip();
    test_full_round_two_players();
    test_props_win_on_time_and_rotation();
    test_gather_timeout();
    test_hit_rules();
    test_late_join_and_leave();
    test_client_and_host_migration();
    test_colors_unique();
    test_maps();
    test_props();
    std::printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
