// Rules test: drives src/match.cpp through whole rounds with a fake network and a hand-cranked
// clock. Build and run with tests/run.sh (no game or Dusklight needed).

#include "fake_net.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "protocol.hpp"
#include "props.hpp"
#include "arena_geometry.hpp"
#include "gameplay.hpp"
#include "rules_config.hpp"
#include "collision_cleanup.hpp"
#include "interpolation.hpp"
#include "hud_layout.hpp"

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

static std::vector<uint8_t> state_msg(const char* stage, float x, float y, float z,
    uint8_t flags = STATE_IN_WORLD, uint8_t prop = 0, int16_t yaw = 0) {
    PlayerState s;
    s.flags = flags;
    copy_str(s.stage, stage);
    s.x = x;
    s.y = y;
    s.z = z;
    s.yaw = yaw;
    s.prop = prop;
    s.propYaw = yaw;
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

static std::vector<uint8_t> place_decoy_msg(uint32_t round) {
    Writer w(MSG_PLACE_DECOY);
    w.u32(round);
    return w.bytes();
}

static std::vector<uint8_t> hit_decoy_msg(uint32_t round, uint8_t id) {
    Writer w(MSG_HIT_DECOY);
    w.u32(round);
    w.u8(id);
    return w.bytes();
}

static void reset_net(int players) {
    g_fake = net::FakeNet{};
    for (int id = 1; id <= players; ++id) g_fake.add(id, ("P" + std::to_string(id)).c_str());
}

static void host_room(int players) {
    reset_net(players);
    match::on_welcome();
    match::Settings defaults;
    CHECK(defaults.autoTaunt && defaults.trackingPulse);
    CHECK(defaults.hideSecs == 30 && defaults.seekSecs == 180);
    CHECK(defaults.idleTauntSecs == 20);
    CHECK(defaults.freeDecoys == 3);
    match::Settings s;
    s.map = 0;
    s.hideSecs = 20;
    s.seekSecs = 60;
    s.autoNext = false;
    s.freeDecoys = 5; // legacy decoy fixtures explicitly request this allowance
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
    s.autoTaunt = true;
    s.idleTauntSecs = 90;
    s.freeDecoys = 99;  // clamps to the host-visible 0..10 range
    s.isPublic = true;
    s.trackingPulse = false;
    Writer sw(MSG_SETTINGS);
    s.write(sw);
    Reader sr(sw.bytes().data() + 1, sw.bytes().size() - 1);
    match::Settings t;
    t.read(sr);
    CHECK(sr.ok());
    CHECK(!t.trackingPulse);
    CHECK(t.mode == Mode::HideAndSeek && t.map == kRandomMap && t.hunters == 2);
    CHECK(t.hideSecs == 10 && t.seekSecs == 1800);
    CHECK(!t.missPenalty && t.isPublic && t.foundJoinHunters && t.autoTaunt && t.autoNext);
    CHECK(t.idleTauntSecs == 90);
    CHECK(t.freeDecoys == 10);
}

static void test_decoy_economy_and_validation() {
    std::printf("bounded decoys, live points, and authoritative hits\n");
    host_room(3);
    match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(3, stage);
    advance(100);
    CHECK(match::get().phase == Phase::Hide);

    int hiders[2] = {};
    int hiderCount = 0;
    const int hunter = hunter_id();
    for (int id = 1; id <= 3; ++id) {
        if (match::player(id).role == Role::Hider) hiders[hiderCount++] = id;
    }
    CHECK(hunter != 0 && hiderCount == 2);

    const auto state = [&](int id, float x, float z, bool disguised) {
        const uint8_t flags = static_cast<uint8_t>(STATE_IN_WORLD | (disguised ? STATE_DISGUISED : 0));
        if (id == g_fake.self) {
            PlayerState s;
            s.flags = flags;
            copy_str(s.stage, stage);
            s.x = x;
            s.z = z;
            s.prop = 0;
            s.propYaw = static_cast<int16_t>(x);
            match::set_local_state(s);
        } else {
            deliver(static_cast<uint8_t>(id), state_msg(stage, x, 0.0f, z, flags, 0,
                                                   static_cast<int16_t>(x)));
        }
    };
    const auto place = [&](int id) {
        if (id == g_fake.self) match::place_decoy();
        else deliver(static_cast<uint8_t>(id), place_decoy_msg(match::get().round));
    };

    // Each hider uses all five free placements. Every placement remains alive; one player's
    // allowance must never evict another player's setup.
    for (int n = 0; n < 5; ++n) {
        for (int h = 0; h < 2; ++h) {
            state(hiders[h], 200.0f + n * 220.0f, h == 0 ? 0.0f : 500.0f, true);
            place(hiders[h]);
        }
        advance(800);
    }
    CHECK(match::decoy_count() == 10);
    CHECK(match::player(hiders[0]).decoysUsed == 5);
    CHECK(match::player(hiders[1]).decoysUsed == 5);
    CHECK(match::player(hiders[0]).roundPoints == 0);

    // There are no paid placements during hiding time.
    state(hiders[0], 1500.0f, 0.0f, true);
    place(hiders[0]);
    CHECK(match::player(hiders[0]).decoysUsed == 5);
    CHECK(match::decoy_count() == 10);

    while (match::get().phase == Phase::Hide) advance(1000);
    CHECK(match::get().phase == Phase::Seek);
    advance(30'000);
    CHECK(match::player(hiders[0]).roundPoints == 3);  // awarded live, so it is spendable

    // The sixth placement costs three points and remains alongside the free placements.
    state(hiders[0], 1700.0f, 0.0f, true);
    place(hiders[0]);
    CHECK(match::player(hiders[0]).decoysUsed == 6);
    CHECK(match::player(hiders[0]).roundPoints == 0);
    CHECK(match::decoy_count() == 11);

    const match::Decoy target = match::decoy(0);
    state(hunter, target.x + 1000.0f, target.z, false);
    deliver(static_cast<uint8_t>(hunter), hit_decoy_msg(match::get().round, target.id));
    CHECK(match::decoy_count() == 11);  // too far
    deliver(static_cast<uint8_t>(hiders[1]), hit_decoy_msg(match::get().round, target.id));
    CHECK(match::decoy_count() == 11);  // hiders cannot clear traps
    state(hunter, target.x, target.z, false);
    deliver(static_cast<uint8_t>(hunter), hit_decoy_msg(match::get().round, target.id));
    CHECK(match::decoy_count() == 10);
}

static void test_full_room_decoy_capacity() {
    std::printf("full room retains every player's decoys\n");
    host_room(kMaxPlayers);
    match::Settings settings = match::get().settings;
    settings.hunters = 1;
    settings.hideSecs = 600;
    settings.freeDecoys = match::kMaxDecoysPerPlayer;
    match::set_settings(settings);
    match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(kMaxPlayers, stage);
    // FakeNet introduces all peers before the host callback is installed, so peers not represented
    // in the synthetic ready roster reach Hide through the normal gather timeout.
    if (match::get().phase == Phase::Gather) advance(26'000);
    CHECK(match::get().phase == Phase::Hide);

    int hiders = 0;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (match::player(id).role != Role::Hider) continue;
        ++hiders;
        for (int n = 0; n < match::kMaxDecoysPerPlayer; ++n) {
            const float x = static_cast<float>(id * 3000 + n * 200);
            const float z = static_cast<float>(id * 3000);
            const auto state = state_msg(stage, x, 0.0f, z, STATE_IN_WORLD | STATE_DISGUISED);
            if (id == g_fake.self) {
                PlayerState local;
                local.flags = STATE_IN_WORLD | STATE_DISGUISED;
                copy_str(local.stage, stage);
                local.x = x;
                local.z = z;
                match::set_local_state(local);
                match::place_decoy();
            } else {
                deliver(static_cast<uint8_t>(id), state);
                deliver(static_cast<uint8_t>(id), place_decoy_msg(match::get().round));
            }
            advance(800);
        }
    }

    CHECK(hiders == kMaxPlayers - 1);
    CHECK(match::decoy_count() == hiders * match::kMaxDecoysPerPlayer);
    CHECK(match::decoy_count() <= match::kMaxActiveDecoys);
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (match::player(id).role == Role::Hider) {
            CHECK(match::player(id).decoysUsed == match::kMaxDecoysPerPlayer);
        }
    }
    size_t snapshotSize = 0;
    for (const auto& sent : g_fake.sent) {
        if (!sent.bytes.empty() && sent.bytes[0] == MSG_DECOYS) snapshotSize = sent.bytes.size();
    }
    CHECK(snapshotSize < 4096);
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
    // A real game continues publishing state throughout the immunity period.
    for (int id = 1; id <= 3; ++id) deliver(static_cast<uint8_t>(id), state_msg(stage, 0, 0, 0));
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
        roster.u8(0);
        roster.u8(0); // rupees
        roster.u8(0); // finds
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
    for (int m = 0; m < map_count(); ++m) {
        const MapInfo& map = map_info(m);
        CHECK(map.stage != nullptr && map.stage[0] != '\0');
        CHECK(std::isfinite(map.spawnX) && std::isfinite(map.spawnY) &&
              std::isfinite(map.spawnZ));
    }
    for (int i = 0; i < 200; ++i) {
        const int m = random_map(3);
        CHECK(m >= 0 && m < map_count() && m != 3);
    }
}

static void test_props() {
    std::printf("prop catalogue\n");
    CHECK(prop_count() == 67);
    for (int i = 0; i < prop_count(); ++i) {
        const PropInfo& prop = prop_info(i);
        CHECK(prop.name != nullptr && prop.arc != nullptr);
        CHECK(prop.bmd != nullptr || prop.bmdIndex >= 0);
        CHECK(prop.radius > 0.0f && prop.height > 0.0f && prop.scale > 0.0f);
        CHECK(prop.simpleShadowSize >= 0.0f && prop.simpleShadowSize <= 200.0f);
        CHECK(std::isfinite(prop.offsetX) && std::isfinite(prop.offsetY) &&
              std::isfinite(prop.offsetZ));
    }
    CHECK(std::strcmp(prop_info(15).name, "Sign") == 0);
    CHECK(std::strcmp(prop_info(20).name, "Gravestone") == 0);
    CHECK(std::strcmp(prop_info(58).name, "Map Table") == 0);
    CHECK(prop_count_for_map(-1) == 57);        // nine unsafe or oversized legacy IDs stay reserved
    CHECK(prop_info(27).simpleShadowSize == 0.0f); // Lily Pad gets no black ground blob
    CHECK(prop_info(5).simpleShadowSize == 70.0f); // pumpkins use a cheap native-sized shadow
    CHECK(prop_info(9).simpleShadowSize == 48.0f); // Cucco no longer redraws into a shadow pass
    CHECK(prop_info(35).simpleShadowSize == 61.0f); // neither does the oil jar
    // Disguises are drawn at the native actor's scale, the size of the real object beside them.
    CHECK(prop_info(2).scale == 0.5f);   // Crate: daObjCarry_c KIBAKO
    CHECK(prop_info(5).scale == 1.4f);   // Pumpkin: daObj_Pumpkin_Param_c
    CHECK(prop_info(7).scale == 0.5f);   // Small Crate: Obj_kbox
    CHECK(prop_info(11).scale == 0.6f);  // Deku Nut: daObjCarry_c BOKKURI
    CHECK(prop_info(19).scale == 1.35f); // Boar Bones: daObjIBone_c
    CHECK(prop_info(20).scale == 1.0f);  // Gravestone
    CHECK(prop_info(45).scale == 2.0f);  // Large Box: daObj_Lbox_HIO_c
    CHECK(!prop_on_map(28, -1) && !prop_on_map(29, -1)); // ice floes are platforms at real size
    CHECK(!prop_on_map(30, -1) && !prop_on_map(31, -1)); // so are the raft and the kelp
    // Collision copies the native actor's.
    CHECK(prop_solid_unmatched() == 0);
    CHECK(prop_solid(0).kind == Solid::Cylinder && prop_solid(0).radius == 30.0f);  // Pot
    CHECK(prop_solid(20).kind == Solid::Background);   // Gravestone
    CHECK(prop_solid(45).kind == Solid::Background);   // Large Box
    CHECK(prop_solid(32).kind == Solid::None);         // Laundry hangs loose
    CHECK(prop_solid(9).kind == Solid::None);          // a real Cucco is pushed aside
    for (int i = 0; i < prop_count(); ++i) {
        const PropSolid& solid = prop_solid(i);
        if (solid.kind == Solid::Background) {
            CHECK(solid.dzb != nullptr || solid.dzbIndex >= 0);
            CHECK(solid.bgScaleX > 0.0f && solid.bgScaleY > 0.0f && solid.bgScaleZ > 0.0f);
        }
        if (solid.kind == Solid::Cylinder) CHECK(solid.radius >= 0.0f);
    }
    CHECK(prop_info(32).scale == 1.0f && prop_info(32).offsetY > 140.0f); // Laundry
    CHECK(prop_info(26).scale == 4.0f && std::fabs(prop_info(26).offsetX) > 2000.0f); // Crystal
    CHECK(!prop_on_map(36, -1)); // environment-sized River Rock
    CHECK(!prop_on_map(43, -1)); // incomplete two-model Board Target
    CHECK(!prop_on_map(47, -1)); // particle-dependent Palace Candle
    CHECK(!prop_on_map(50, -1)); // actor-placed flat Desert Fence rail
    CHECK(!prop_on_map(57, -1)); // composite Lake Buoy
    CHECK(prop_for_carry_type(3) == 10);   // cannonball
    CHECK(prop_for_carry_type(6) == 11);   // Deku nut
    CHECK(prop_for_carry_type(10) == 12);  // big blue pot
    CHECK(prop_for_carry_type(12) == 13);  // small Twilight pot
    CHECK(prop_for_carry_type(13) == 14);  // big Twilight pot
    CHECK(prop_for_carry_type(99) == -1);
    for (int i = 0; i < 200; ++i) {
        const int prop = random_prop();
        CHECK(prop >= 0 && prop < prop_count() && prop_on_map(prop, -1));
    }
    for (int map = 0; map < map_count(); ++map) {
        CHECK(prop_count_for_map(map) >= 15);
        int current = prop_for_map(map, 0);
        CHECK(prop_on_map(current, map));
        const int next = step_prop(current, map, 1);
        const int previous = step_prop(current, map, -1);
        CHECK(next != current && previous != current);
        CHECK(prop_on_map(next, map) && prop_on_map(previous, map));
        for (int i = 0; i < 50; ++i) {
            const int prop = random_prop(map);
            CHECK(prop >= 0 && prop < prop_count() && prop_on_map(prop, map));
        }
    }
}

static void test_balanced_rules() {
    std::printf("balanced rules and saved-rule migration\n");
    const auto fresh = settings::parse_rules("");
    CHECK(fresh.hideSecs == 30 && fresh.seekSecs == 180 && fresh.autoTaunt);
    CHECK(fresh.trackingPulse && fresh.idleTauntSecs == 20);
    const auto upgraded = settings::parse_rules(settings::upgrade_rules("0,14,45,240,0,27,60,10"));
    CHECK(upgraded.hideSecs == 30 && upgraded.seekSecs == 180);
    CHECK(upgraded.autoTaunt && upgraded.trackingPulse && upgraded.idleTauntSecs == 20);
    CHECK(upgraded.map == 14 && upgraded.isPublic && upgraded.freeDecoys == 10);
    const auto legacy = settings::parse_rules(settings::upgrade_rules("0,255,45,240,0,11"));
    CHECK(legacy.hideSecs == 30 && legacy.autoTaunt && legacy.idleTauntSecs == 20);
    const auto custom = settings::parse_rules(settings::upgrade_rules("1,9,75,360,3,16,0,7"));
    CHECK(custom.hideSecs == 75 && custom.seekSecs == 360 && custom.hunters == 3);
    CHECK(!custom.autoTaunt && !custom.missPenalty && custom.idleTauntSecs == 0);
    CHECK(custom.mode == Mode::HideAndSeek && custom.map == 9 && custom.freeDecoys == 7);
    auto changed = fresh;
    changed.trackingPulse = false;
    CHECK(!settings::parse_rules(settings::format_rules(changed)).trackingPulse);
    CHECK(settings::format_rules(settings::parse_rules(settings::format_rules(custom))) == settings::format_rules(custom));
    CHECK(life_after_miss(20) == 19 && life_after_miss(2) == 1 && life_after_miss(1) == 1);
    CHECK(life_after_miss(0) == 0 && kArenaHeartPieces / 5 * 4 == kArenaLife);
    CHECK(kTauntRevealMs == 3000 && kTauntCooldownMs == 4000);
    for (int n = 2; n <= kMaxPlayers; ++n) {
        for (int map = 0; map < map_count(); ++map) {
            const int hunters = recommended_hunters(n, map);
            CHECK(hunters >= 1 && hunters < n);
            CHECK(clue_interval_ms(map, 60000) == 10000);
            CHECK(clue_interval_ms(map, 180000) == (map_info(map).large ? 20000 : 30000));
        }
    }
    CHECK(recommended_hunters(4, 0) == 1 && recommended_hunters(4, 9) == 2);
    CHECK(recommended_hunters(16, 14) == 6);
    for (int i = 0; i < 100; ++i) {
        const int selected = random_map(0, 2);
        CHECK(selected != 0 && !map_info(selected).large);
    }
    host_room(4);
    match::Settings rules;
    rules.map = 9;
    rules.autoNext = false;
    match::set_settings(rules);
    match::start_round();
    CHECK(match::get().map == 9 && match::count_role(Role::Hunter) == 2);
    match::end_round();
    rules.hunters = 1;
    match::set_settings(rules);
    match::start_round();
    CHECK(match::count_role(Role::Hunter) == 1);
}

static void test_exit_collision() {
    std::printf("arena collision: rotated exits, height, corners and swept rolls\n");
    using namespace hs::arena;
    const ExitVolume box{{100, 0, 0}, 10, 300, 500, 0, 1};
    CHECK(overlaps_exit(box, {70, 0, 0}));
    CHECK(!overlaps_exit(box, {40, 0, 0}));
    CHECK(!overlaps_exit(box, {100, 301, 0}));
    CHECK(!overlaps_exit(box, {100, -151, 0}));
    const ExitVolume rotated{{0, 0, 0}, 10, 300, 500, 1, 0};
    CHECK(overlaps_exit(rotated, {400, 0, 0}));
    CHECK(!overlaps_exit(rotated, {0, 0, 400}));
    const ExitVolume grotto{{0, 0, 0}, 100, 0, 0, 0, 1, true};
    CHECK(overlaps_exit(grotto, {130, 2000, 0}));
    CHECK(!overlaps_exit(grotto, {140, 0, 0}));
    const auto collide = [&](Point p) { return overlaps_exit(box, p); };
    const auto rolled = sweep({0, 0, 0}, {300, 0, 0}, collide);
    CHECK(rolled.x > 0 && rolled.x < 55);
    const auto jumped = sweep({0, 0, 0}, {300, 100, 0}, collide);
    CHECK(jumped.x < 55 && jumped.y < 100);
    const auto away = sweep({0, 0, 0}, {-300, 0, 0}, collide);
    CHECK(away.x == -300);
    const auto side = sweep({0, 0, 600}, {300, 0, 600}, collide);
    CHECK(side.x == 300);
    const ExitVolume corner{{0, 0, 0}, 10, 300, 10, 0, 1};
    CHECK(!overlaps_exit(corner, {40, 0, 40}));
    CHECK(overlaps_exit(corner, {30, 0, 30}));
    // Reproduce an actor with embedded shapes being deleted before the retained collision pass.
    unsigned char actorStorage[256]{};
    unsigned char otherStorage[64]{};
    unsigned char* retained[] = {actorStorage, actorStorage + 64, otherStorage, nullptr, actorStorage + 255};
    uint16_t count = 5;
    CHECK(clear_actor_collision(retained, count, actorStorage, sizeof(actorStorage)) == 3);
    CHECK(count == 1 && retained[0] == otherStorage);
    CHECK(retained[1] == nullptr && retained[2] == nullptr && retained[4] == nullptr);
}

static void test_treasure_and_clues() {
    std::printf("host validates treasure and confirms clues\n");
    host_room(3);
    auto rules = match::get().settings;
    rules.seekSecs = 180;
    match::set_settings(rules);
    match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(3, stage);
    advance(21'000);
    const int hider = hider_id(), hunter = hunter_id();
    const auto fresh = [&](int id, float x = 0.0f) {
        deliver(static_cast<uint8_t>(id), state_msg(stage, x, 0, 0, STATE_IN_WORLD | STATE_DISGUISED));
    };
    const auto request = [&](int id, uint8_t type, uint16_t item = 0) {
        Writer w(type); w.u32(match::get().round);
        if (type == MSG_TAUNT) w.u8(0); else w.u16(item);
        deliver(static_cast<uint8_t>(id), w.bytes());
    };
    fresh(hider); fresh(hunter);
    CHECK(!match::spawn_rupee(NAN, 0, 0));
    CHECK(!match::spawn_rupee(10000, 0, 0));
    CHECK(match::spawn_rupee(700, 0, 0));
    auto item = match::get().rupees[0].id;
    request(hider, MSG_COLLECT_RUPEE, item); // too far
    CHECK(match::get().rupeeCount == 1 && match::player(hider).rupeesCollected == 0);
    fresh(hunter, 700); request(hunter, MSG_COLLECT_RUPEE, item);
    CHECK(match::get().rupeeCount == 1); // hunters cannot collect
    fresh(hider, 700); request(hider, MSG_COLLECT_RUPEE, item);
    CHECK(match::get().rupeeCount == 0 && match::player(hider).rupeesCollected == 1);
    CHECK(match::player(hider).roundPoints == 3);
    CHECK(match::player(hider).revealedUntil > s_now);
    request(hider, MSG_COLLECT_RUPEE, item);
    CHECK(match::player(hider).roundPoints == 3); // consumed, no duplicate reward
    for (int n = 0; n < 2; ++n) {
        const float x = 1400 + n * 700;
        fresh(hider, x - 700);
        CHECK(match::spawn_rupee(x, 0, 0));
        item = match::get().rupees[0].id;
        fresh(hider, x); request(hider, MSG_COLLECT_RUPEE, item);
    }
    CHECK(match::player(hider).rupeesCollected == 3);
    CHECK(match::player(hider).roundPoints == 14); // 3*3 + one collection bonus
    advance(5000); fresh(hider, 2100);
    const auto points = match::player(hider).roundPoints;
    request(hider, MSG_TAUNT);
    CHECK(match::player(hider).roundPoints == points + 1);
    const auto reveal = match::player(hider).revealedUntil;
    request(hider, MSG_TAUNT);
    CHECK(match::player(hider).roundPoints == points + 1 && match::player(hider).revealedUntil == reveal);
    Writer forged(MSG_CLUE); forged.u32(match::get().round); forged.u8(hunter); forged.u8(0); forged.u8(0);
    deliver(static_cast<uint8_t>(hider == g_fake.host ? hunter : hider), forged.bytes());
    CHECK(match::player(hunter).revealedUntil == 0);
    advance(21000); fresh(hider, 2100); match::update();
    CHECK(match::player(hider).revealedUntil > s_now); // host, rather than hider, enforces automatic clue
    const auto before = match::player(hider).roundPoints;
    CHECK(before == points + 3); // survival awards only, automatic taunts give no points
    CHECK(match::spawn_rupee(2800, 0, 0));
    item = match::get().rupees[0].id;
    Writer stale(MSG_COLLECT_RUPEE); stale.u32(match::get().round - 1); stale.u16(item);
    fresh(hider, 2800); deliver(static_cast<uint8_t>(hider), stale.bytes());
    CHECK(match::get().rupeeCount == 1);
    advance(match::kRupeeLifetimeMs + 100);
    CHECK(match::get().rupeeCount == 0);
    match::end_round();
    CHECK(match::get().rupeeCount == 0);
}

static void test_interpolation_and_compact_states() {
    std::printf("buffered movement, bounded prediction and compact states\n");
    PlayerState a; a.flags = STATE_IN_WORLD | STATE_DISGUISED | STATE_COMPACT;
    copy_str(a.stage, "F_SP103");
    Writer w(MSG_STATE); a.write(w);
    CHECK(w.bytes().size() == 28);
    Reader r(w.bytes().data() + 1, w.bytes().size() - 1); PlayerState decoded; decoded.read(r);
    CHECK(r.ok() && decoded.flags == a.flags && decoded.under[0].idx == 0xFFFF);
    PlayerState b = a; b.x = 100; a.yaw = 32760; b.yaw = -32760;
    const auto middle = interpolate_state(a, 1000, b, 1100, 1150);
    CHECK(std::fabs(middle.x - 50) < 0.01f);
    CHECK(std::abs(middle.yaw) > 32000); // crosses wrap by the short path
    const auto bounded = interpolate_state(a, 1000, b, 1100, 100000);
    CHECK(bounded.x == 200); // bounded extrapolation, no runaway stale movement
    b.x = 10000;
    CHECK(interpolate_state(a, 1000, b, 1100, 1150).x == 10000); // teleports snap
    CHECK(prop_on_map(59, 1) && !prop_on_map(59, 10));
    CHECK(prop_on_map(60, 12) && !prop_on_map(60, 13));
    CHECK(prop_on_map(61, 10) && !prop_on_map(61, 1));
    CHECK(!prop_on_map(rupee_prop(), -1));

    // Moving players must stay smooth in the lobby as well as during rounds. Idle traffic drops.
    reset_net(2); match::on_welcome(); g_fake.sent.clear();
    PlayerState wire; wire.flags = STATE_IN_WORLD; copy_str(wire.stage, "F_SP103");
    match::set_local_state(wire);
    CHECK(count_sent(MSG_STATE) == 1);
    s_now += 100; wire.x += 10; match::set_local_state(wire);
    CHECK(count_sent(MSG_STATE) == 2);
    s_now += 100; match::set_local_state(wire);
    CHECK(count_sent(MSG_STATE) == 2);
    s_now += 900; match::set_local_state(wire);
    CHECK(count_sent(MSG_STATE) == 3);
}

static void test_mobile_hud_layout() {
    std::printf("mobile HUD bounds and label collision\n");
    const float sizes[][2] = {{320, 240}, {608, 448}, {844, 390}, {390, 844}, {1280, 720}};
    for (const auto& size : sizes) {
        const auto top = hud::centre_card(12, 24, size[0], 8, 40);
        const auto footer = hud::centre_card(12, 24, size[0], size[1] - 56, 40);
        CHECK(top.x >= 12 && top.x + top.w <= 12 + size[0]);
        CHECK(footer.y >= 24 && footer.y + footer.h <= 24 + size[1]);
        CHECK(!top.overlaps(footer));
        for (int players = 1; players <= 16; ++players) {
            const auto scores = hud::score_layout(12, 24, size[0], size[1], players);
            CHECK(scores.card.x >= 12 && scores.card.x + scores.card.w <= 12 + size[0]);
            CHECK(scores.card.y >= 24 && scores.card.y + scores.card.h <= 24 + size[1]);
            CHECK(scores.rowHeight > 0 && scores.card.h >= scores.rowHeight * players);
        }
    }
    CHECK((hud::Rect{0, 0, 20, 20}.overlaps(hud::Rect{18, 8, 20, 20})));
    CHECK((!hud::Rect{0, 0, 20, 20}.overlaps(hud::Rect{28, 0, 20, 20})));
}

int main() {
    test_treasure_and_clues();
    test_interpolation_and_compact_states();
    test_mobile_hud_layout();
    test_protocol_roundtrip();
    test_decoy_economy_and_validation();
    test_full_room_decoy_capacity();
    test_full_round_two_players();
    test_props_win_on_time_and_rotation();
    test_gather_timeout();
    test_hit_rules();
    test_late_join_and_leave();
    test_client_and_host_migration();
    test_colors_unique();
    test_maps();
    test_props();
    test_balanced_rules();
    test_exit_collision();
    std::printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
