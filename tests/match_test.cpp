// Rules test: drives src/match.cpp through whole rounds with a fake network and a hand-cranked
// clock. Build and run with tests/run.sh (no game or Dusklight needed).

#include "fake_net.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "protocol.hpp"
#include "props.hpp"
#include "rarc.hpp"
#include "arena_geometry.hpp"
#include "gameplay.hpp"
#include "rules_config.hpp"
#include "collision_cleanup.hpp"
#include "interpolation.hpp"
#include "hud_layout.hpp"
#include "scoring.hpp"
#include "treasure_layout.hpp"

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
    CHECK(defaults.finalClueSecs == 20 && defaults.trackingPulse && defaults.missPenaltyQuarters == 2);
    CHECK(defaults.hideSecs == 30 && defaults.seekSecs == 180);
    CHECK(defaults.idleTauntSecs == 0);
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
    s.decoySwap = false;
    s.map = kRandomMap;
    s.hideSecs = 5;      // clamps to 10
    s.seekSecs = 60000;  // clamps to 1800
    s.hunters = 2;
    s.missPenaltyQuarters = 0;
    s.finalClueSecs = 35;
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
    CHECK(!t.decoySwap && t.map == kRandomMap && t.hunters == 2);
    CHECK(t.hideSecs == 10 && t.seekSecs == 1800);
    CHECK(t.missPenaltyQuarters == 0 && t.isPublic && t.foundJoinHunters && t.finalClueSecs == 35 && t.autoNext);
    CHECK(t.idleTauntSecs == 90);
    CHECK(t.freeDecoys == 10);

    // A partial settings packet must not turn a saved penalty/finale Off or partially apply it.
    host_room(2);
    const auto original = settings::format_rules(match::get().settings);
    for (size_t length = 1; length < sw.bytes().size(); ++length) {
        match::on_message(g_fake.host, sw.bytes().data(), length);
        CHECK(settings::format_rules(match::get().settings) == original);
    }
    s.missPenaltyQuarters = 255; s.finalClueSecs = 65535;
    Writer extreme(MSG_SETTINGS); s.write(extreme);
    deliver(g_fake.host, extreme.bytes());
    CHECK(match::get().settings.missPenaltyQuarters == 4 && match::get().settings.finalClueSecs == 60);
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
    CHECK(match::player(hiders[0]).roundPoints == 6);  // half of a 60-second hunt, spendable live

    // The sixth placement costs three points and remains alongside the free placements.
    state(hiders[0], 1700.0f, 0.0f, true);
    place(hiders[0]);
    CHECK(match::player(hiders[0]).decoysUsed == 6);
    CHECK(match::player(hiders[0]).roundPoints == 3);
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
    advance(30'000);  // half of the 60-second hunt: 6 points
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
    CHECK(match::player(hunter).roundPoints == 12 + scoring::kBonusPoints);
    CHECK(match::player(hider).roundPoints == 6);

    advance(100);
    CHECK(match::get().phase == Phase::Results);
    CHECK(match::get().winner == 1);
    CHECK(match::player(hunter).roundPoints == 12 + scoring::kBonusPoints + 6);
    CHECK(match::player(hider).roundPoints == 6); // joining hunters earns no second win bonus
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
        else CHECK(match::player(id).roundPoints == 12 + 6);  // full survival + win
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
    CHECK(match::player(3).startingRole == Role::None);
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
        roster.u16(id == 1 ? 0 : 9);
        roster.u16(id == 1 ? 0 : 6); // three objective + six bonus, with three spent
        roster.u8(id == 1 ? 4 : 8);
        roster.u8(id == 1 ? 1 : 0);
        roster.u8(0);
        roster.u8(0); // rupees
        roster.u8(0); // finds
        roster.u8(id == 1 ? 0 : 6); // gross bonus
        roster.u8(id == 1 ? 0 : 3); // survival objective already awarded at 30 seconds
        roster.u8(id == 1 ? kArenaLife : 0);
        roster.u16(0);
        for (int stat = 0; stat < 5; ++stat) roster.u8(0);
        for (int cooldown = 0; cooldown < 3; ++cooldown) roster.u16(0);
    }
    deliver(1, roster.bytes());
    Writer round(MSG_ROUND);
    round.u32(7);
    round.u8(2);
    round.u16(30);
    round.u16(120);
    round.u8(2); round.u8(0);
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
    CHECK(match::player(2).roundPoints == 12 && match::player(2).bonusEarned == 6);
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
    std::printf("native prop catalogue and per-arena palettes\n");
    CHECK(prop_count() == kPropCount);
    CHECK(prop_count_for_map(-1) == kPropCount - 1);
    for (int i = 0; i < prop_count(); ++i) {
        const PropInfo& prop = prop_info(i);
        CHECK(prop.name != nullptr && prop.arc != nullptr && prop.model.set());
        CHECK(prop.radius > 0.0f && prop.height > 0.0f && prop.scale > 0.0f);
        CHECK(std::isfinite(prop.offsetY) && std::isfinite(prop.shadowSize));
        CHECK(prop.shadowSize >= 0.0f);
        CHECK(std::strcmp(prop.name, "Crystal") != 0); // invisible legacy disguise is removed
        if (prop.solid.kind == Solid::Background) {
            CHECK(prop.solid.dzb.set() && prop.solid.bgScale > 0.0f);
        }
        if (prop.solid.kind == Solid::Cylinder) CHECK(prop.solid.radius >= 0.0f);
    }
    CHECK(prop_info(kCrate).scale == 0.5f && prop_info(kCrate).shadow == Shadow::Square);
    CHECK(prop_info(kPumpkin).scale == 1.4f && prop_info(kPumpkin).shadowSize == 50.0f);
    CHECK(prop_info(kLilyPad).motion == Motion::LilyPad && prop_info(kLilyPad).offsetY == 0);
    CHECK(prop_info(kLilyPad).shadow == Shadow::None);
    CHECK(prop_info(kHorseGrass).motion == Motion::Sway && prop_info(kHorseGrass).shadow == Shadow::None);
    CHECK(prop_info(kCucco).shadow == Shadow::Real && prop_info(kOilJar).extra.set());
    CHECK(prop_info(kPot).solid.kind == Solid::Cylinder && prop_info(kPot).solid.radius == 30);
    CHECK(prop_info(kGravestone).solid.kind == Solid::Background);
    CHECK(prop_info(kCucco).solid.kind == Solid::None);
    CHECK(prop_for_native(Native::Carry, 0) == kPot);
    CHECK(prop_for_native(Native::Carry, 10) == kBlueBigPot);
    CHECK(prop_for_native(Native::Carry, 99) == -1);
    CHECK(prop_for_native(Native::CallGrass, 1) == kHorseGrass);
    CHECK(!prop_selectable(kTreasureRupee) && !prop_selectable(-1));
    for (int map = -1; map < map_count(); ++map) {
        const int count = prop_count_for_map(map);
        CHECK(count >= 6);
        bool seen[kPropCount] = {};
        for (int ordinal = 0; ordinal < count; ++ordinal) {
            const int prop = prop_for_map(map, ordinal);
            CHECK(prop_selectable(prop) && prop_on_map(prop, map));
            CHECK(!seen[prop]); seen[prop] = true;
            CHECK(step_prop(prop, map, 1) == prop_for_map(map, ordinal + 1));
            CHECK(step_prop(prop, map, -1) == prop_for_map(map, ordinal - 1));
        }
        for (int i = 0; i < 50; ++i) CHECK(prop_on_map(random_prop(map), map));
        CHECK(!prop_on_map(kTreasureRupee, map));
    }
}

static void test_balanced_rules() {
    std::printf("balanced rules and saved-rule migration\n");
    const auto fresh = settings::parse_rules("");
    CHECK(fresh.hideSecs == 30 && fresh.seekSecs == 180 && fresh.finalClueSecs == 20);
    CHECK(fresh.trackingPulse && fresh.idleTauntSecs == 0 && fresh.missPenaltyQuarters == 2);
    const auto upgraded = settings::parse_legacy_rules(settings::upgrade_rules("0,14,45,240,0,27,60,10"));
    CHECK(upgraded.hideSecs == 30 && upgraded.seekSecs == 180);
    CHECK(upgraded.finalClueSecs == 20 && upgraded.trackingPulse && upgraded.idleTauntSecs == 20);
    CHECK(upgraded.map == 14 && upgraded.isPublic && upgraded.freeDecoys == 10);
    const auto legacy = settings::parse_legacy_rules(settings::upgrade_rules("0,255,45,240,0,11"));
    CHECK(legacy.hideSecs == 30 && legacy.finalClueSecs == 20 && legacy.idleTauntSecs == 20);
    const auto custom = settings::parse_legacy_rules(settings::upgrade_rules("1,9,75,360,3,16,0,7"));
    CHECK(custom.hideSecs == 75 && custom.seekSecs == 360 && custom.hunters == 3);
    CHECK(custom.finalClueSecs == 0 && custom.missPenaltyQuarters == 0 && custom.idleTauntSecs == 0);
    CHECK(custom.map == 9 && custom.freeDecoys == 7);
    auto changed = fresh;
    changed.trackingPulse = false;
    CHECK(!settings::parse_rules(settings::format_rules(changed)).trackingPulse);
    CHECK(settings::format_rules(settings::parse_rules(settings::format_rules(custom))) == settings::format_rules(custom));
    const auto stock = settings::parse_legacy_rules(settings::upgrade_balance_rules("0,14,30,180,0,111,20,3"));
    CHECK(stock.idleTauntSecs == 30 && stock.map == 14);
    const auto kept = settings::parse_legacy_rules(settings::upgrade_balance_rules("1,9,75,360,3,16,20,7"));
    CHECK(kept.idleTauntSecs == 20 && kept.seekSecs == 360 && kept.finalClueSecs == 0 && kept.freeDecoys == 7);
    const auto search = settings::parse_legacy_rules(settings::upgrade_search_rules("0,14,30,180,0,111,30,3"));
    CHECK(search.idleTauntSecs == 0 && search.finalClueSecs == 20 && search.missPenaltyQuarters == 2);
    CHECK(search.map == 14 && search.foundJoinHunters && search.treasure);
    const auto customSearch = settings::parse_legacy_rules(settings::upgrade_search_rules("1,9,75,360,3,16,20,7"));
    CHECK(customSearch.idleTauntSecs == 20 && customSearch.finalClueSecs == 0 && customSearch.missPenaltyQuarters == 0);
    const auto pipeline = settings::upgrade_balance_rules(settings::upgrade_treasure_rules(
        settings::upgrade_rules("0,255,45,240,0,11")));
    CHECK(settings::parse_legacy_rules(settings::upgrade_search_rules(pipeline, true)).idleTauntSecs == 0);
    changed.idleTauntSecs = 30; changed.missPenaltyQuarters = 4; changed.finalClueSecs = 0;
    const auto explicitRules = settings::format_rules(changed);
    CHECK(settings::parse_rules(explicitRules).decoySwap == changed.decoySwap);
    CHECK(settings::parse_rules(explicitRules).missPenaltyQuarters == 4);
    CHECK(settings::parse_rules("255,30,180,0,111,0,3,99,999").missPenaltyQuarters == 4);
    CHECK(settings::parse_rules("255,30,180,0,111,0,3,-1,-1").finalClueSecs == 0);
    // v0.4.1 dropped Snowpeak Ruins and Arbiter's Grounds; Hyrule Castle Grounds moved to 10.
    CHECK(map_count() == 11 && std::strcmp(map_info(10).name, "Hyrule Castle Grounds") == 0);
    CHECK(settings::parse_rules(settings::upgrade_map_list_rules("12,45,240,2,107,0,5,1,20")).map == 10);
    CHECK(settings::parse_rules(settings::upgrade_map_list_rules("10,45,240,2,107,0,5,1,20")).map == kRandomMap);
    CHECK(settings::parse_rules(settings::upgrade_map_list_rules("11,45,240,2,107,0,5,1,20")).map == kRandomMap);
    const auto movedRules = settings::parse_rules(settings::upgrade_map_list_rules("9,45,240,2,107,0,5,1,20"));
    CHECK(movedRules.map == 9 && movedRules.hideSecs == 45 && movedRules.hunters == 2 && movedRules.freeDecoys == 5);
    CHECK(settings::upgrade_map_list_rules("") == "" && settings::upgrade_map_list_rules("3,1") == "3,1");
    CHECK(life_after_miss(20) == 18 && life_after_miss(2) == 0 && life_after_miss(1) == 0);
    CHECK(life_after_miss(0) == 0 && kArenaHeartPieces / 5 * 4 == kArenaLife);
    for (uint8_t penalty = 0; penalty <= 4; ++penalty) {
        uint16_t life = kArenaLife;
        for (int miss = 0; miss < 30; ++miss) {
            const auto next = life_after_miss(life, penalty);
            CHECK(next <= life);
            CHECK(penalty == 0 ? next == life : (life == 0 || next < life));
            life = next;
        }
        CHECK(life == (penalty == 0 ? kArenaLife : 0));
    }
    CHECK(kTauntRevealMs == 3000 && kTauntCooldownMs == 4000);
    for (int n = 2; n <= kMaxPlayers; ++n) {
        for (int map = 0; map < map_count(); ++map) {
            const int hunters = recommended_hunters(n, map);
            CHECK(hunters >= 1 && hunters < n);
        }
    }
    CHECK(recommended_hunters(4, 0) == 1 && recommended_hunters(4, 8) == 2);
    CHECK(recommended_hunters(16, 10) == 6);
    for (int i = 0; i < 100; ++i) {
        const int selected = random_map(0, 2);
        CHECK(selected != 0 && !map_info(selected).large);
    }
    host_room(4);
    match::Settings rules;
    rules.map = 8;
    rules.autoNext = false;
    match::set_settings(rules);
    match::start_round();
    CHECK(match::get().map == 8 && match::count_role(Role::Hunter) == 2);
    match::end_round();
    rules.hunters = 1;
    match::set_settings(rules);
    match::start_round();
    CHECK(match::count_role(Role::Hunter) == 1);
}

static std::vector<uint8_t> miss_msg(uint32_t round, uint16_t sequence) {
    Writer w(MSG_MISS); w.u32(round); w.u16(sequence);
    return w.bytes();
}

static void test_hunter_elimination() {
    std::printf("hunter hearts reach zero, and the last hunter out gives hiders the win\n");
    for (uint8_t penalty = 0; penalty <= 4; ++penalty) {
        host_room(4);
        auto rules = match::get().settings;
        rules.hunters = 2; rules.missPenaltyQuarters = penalty;
        match::set_settings(rules); match::start_round();
        everyone_ready(4, map_info(match::get().map).stage);
        advance(21'000);
        const int hunter = hunter_id();
        const int hider = hider_id();
        int other = 0;
        for (int id = 1; id <= 4; ++id)
            if (id != hunter && match::player(id).role == Role::Hunter) other = id;
        CHECK(match::player(hunter).hunterLife == 20 && match::player(other).hunterLife == 20);
        deliver(hider, miss_msg(match::get().round, 10)); // props cannot report hunter damage
        CHECK(match::player(hider).missSequence == 0);
        deliver(hunter, miss_msg(match::get().round - 1, 1));
        deliver(hunter, miss_msg(match::get().round, 4097));
        Writer truncated(MSG_MISS); truncated.u32(match::get().round); deliver(hunter, truncated.bytes());
        CHECK(match::player(hunter).hunterLife == 20 && match::player(hunter).missSequence == 0);
        const unsigned misses = penalty ? (20 + penalty - 1) / penalty : 20;
        for (unsigned n = 1; n <= misses; ++n) {
            deliver(hunter, miss_msg(match::get().round, n));
            CHECK(match::player(hunter).hunterLife == std::max(0, 20 - int(n * penalty)));
            CHECK(match::get().phase == Phase::Seek); // the other hunter is still alive
            deliver(hunter, miss_msg(match::get().round, n)); // duplicate, never charged twice
            CHECK(match::player(hunter).hunterLife == std::max(0, 20 - int(n * penalty)));
        }
        if (penalty == 0) {
            CHECK(match::count_role(Role::Hunter) == 2 && count_sent(MSG_HUNTER_OUT) == 0);
            continue;
        }
        CHECK(match::player(hunter).eliminated && match::player(hunter).role == Role::Spectator);
        CHECK(match::count_role(Role::Hunter) == 1 && count_sent(MSG_HUNTER_OUT) == 1);
        // Elimination cannot be reversed by replaying a tag, a state packet or a miss.
        deliver(hunter, state_msg(map_info(match::get().map).stage, 0, 0, 0));
        deliver(hider, state_msg(map_info(match::get().map).stage, 0, 0, 0));
        deliver(hunter, hit_msg(match::get().round, hider));
        deliver(hunter, miss_msg(match::get().round, misses + 1));
        CHECK(!match::player(hider).found && match::player(hunter).hunterLife == 0);
        deliver(other, miss_msg(match::get().round, misses)); // cumulative report, including pending misses
        CHECK(match::player(other).eliminated && match::count_role(Role::Hunter) == 0);
        CHECK(match::get().phase == Phase::Results && match::get().winner == 0);
        CHECK(count_sent(MSG_RESULTS) == 1 && count_sent(MSG_HUNTER_OUT) == 2);
        CHECK(match::player(hider).roundPoints >= scoring::kWinPoints);
        deliver(other, miss_msg(match::get().round, misses));
        CHECK(count_sent(MSG_RESULTS) == 1); // no second victory/score award
        match::start_round();
        for (int id = 1; id <= 4; ++id) {
            const auto& p = match::player(id);
            CHECK(!p.eliminated && p.missSequence == 0);
            CHECK(p.hunterLife == (p.role == Role::Hunter ? kArenaLife : 0));
        }
    }
    // The usual two-player lobby also ends immediately on the one hunter's final miss.
    host_room(2); match::start_round();
    everyone_ready(2, map_info(match::get().map).stage); advance(21'000);
    deliver(hunter_id(), miss_msg(match::get().round, 10));
    CHECK(match::get().phase == Phase::Results && match::get().winner == 0);
}

static void test_find_healing() {
    std::printf("only host-confirmed finds restore hunter life, with a five-heart cap\n");
    for (bool infection : {false, true}) {
        host_room(4);
        auto rules = match::get().settings; rules.hunters = 1; rules.foundJoinHunters = infection;
        match::set_settings(rules); match::start_round();
        const char* stage = map_info(match::get().map).stage;
        everyone_ready(4, stage); advance(21'000);
        const int hunter = hunter_id();
        deliver(hunter, miss_msg(match::get().round, 3));
        CHECK(match::player(hunter).hunterLife == 14);
        int target = hider_id();
        deliver(hunter, hit_msg(match::get().round, target)); // invalid/stale/out of reach
        CHECK(match::player(hunter).hunterLife == 14);
        for (int id = 1; id <= 4; ++id) deliver(id, state_msg(stage, 0, 0, 0));
        CHECK(match::player(hunter).hunterLife == 14); // STATE is never a healing authority
        deliver(hunter, hit_msg(match::get().round, target));
        CHECK(match::player(target).found && match::player(hunter).hunterLife == 18);
        CHECK(match::player(target).hunterLife == (infection ? kArenaLife : 0));
        deliver(hunter, hit_msg(match::get().round, target));
        CHECK(match::player(hunter).hunterLife == 18); // repeated find cannot heal again
        deliver(hunter, hit_msg(match::get().round, hider_id()));
        CHECK(match::player(hunter).hunterLife == 20);
        deliver(hunter, miss_msg(match::get().round, 2)); // older sequence cannot undo damage
        CHECK(match::player(hunter).hunterLife == 20 && match::player(hunter).missSequence == 3);
    }
    host_room(2);
    auto rules = match::get().settings;
    match::set_settings(rules); match::start_round();
    everyone_ready(2, map_info(match::get().map).stage); advance(21'000);
    const int lastHunter = hunter_id();
    deliver(lastHunter, miss_msg(match::get().round, 10));
    CHECK(match::player(lastHunter).hunterLife == 0 && match::get().phase == Phase::Results);
}

static void test_hunter_health_migration() {
    std::printf("hunter life, elimination and pending misses survive host migration\n");
    host_room(4);
    auto rules = match::get().settings; rules.hunters = 2;
    match::set_settings(rules); match::start_round();
    everyone_ready(4, map_info(match::get().map).stage); advance(21'000);
    const int dead = hunter_id();
    deliver(dead, miss_msg(match::get().round, 10));
    const int living = hunter_id();
    deliver(living, miss_msg(match::get().round, 8));
    std::vector<uint8_t> roster, round;
    for (const auto& sent : g_fake.sent) {
        if (sent.bytes[0] == MSG_ROSTER) roster = sent.bytes;
        if (sent.bytes[0] == MSG_ROUND) round = sent.bytes;
    }
    CHECK(roster.size() == 2 + 4 * 28);
    reset_net(4); g_fake.self = living; g_fake.host = living == 1 ? 2 : 1;
    match::on_welcome();
    deliver(g_fake.host, roster); deliver(g_fake.host, round);
    Writer phase(MSG_PHASE); phase.u32(match::get().round);
    phase.u8(static_cast<uint8_t>(Phase::Seek)); phase.u32(45'000); deliver(g_fake.host, phase.bytes());
    CHECK(match::player(dead).eliminated && match::player(dead).hunterLife == 0);
    CHECK(match::my_hunter_life() == 4 && match::player(living).missSequence == 8);
    g_fake.sent.clear();
    match::report_miss();
    CHECK(match::my_hunter_life() == 2 && match::player(living).hunterLife == 4);
    match::report_miss();
    CHECK(match::my_hunter_life() == 0 && count_sent(MSG_MISS) == 2);
    match::report_miss(); match::report_hit(hider_id()); match::report_decoy_hit(1);
    CHECK(count_sent(MSG_MISS) == 2 && count_sent(MSG_HIT) == 0 && count_sent(MSG_HIT_DECOY) == 0);
    // A partial acknowledgement leaves the last miss predicted, without double charging.
    const auto offset = 2 + (living - 1) * 28;
    roster[offset + 14] = 2; roster[offset + 15] = 9;
    deliver(g_fake.host, roster);
    CHECK(match::my_hunter_life() == 0 && match::player(living).missSequence == 9);
    auto forged = roster; forged[offset + 14] = 20;
    deliver(living, forged); // another player cannot publish life/role updates
    CHECK(match::player(living).hunterLife == 2);
    Writer out(MSG_HUNTER_OUT); out.u32(match::get().round); out.u8(living);
    deliver(living, out.bytes());
    CHECK(!match::player(living).eliminated);
    int next = 1;
    while (next == living || next == g_fake.host) ++next;
    g_fake.host = next; match::on_host_changed(next);
    CHECK(count_sent(MSG_MISS) == 3); // resend the unacknowledged cumulative count
    Reader pending(g_fake.sent.back().bytes.data() + 1, g_fake.sent.back().bytes.size() - 1);
    CHECK(pending.u32() == match::get().round && pending.u16() == 10);
    g_fake.host = living; match::on_host_changed(living);
    CHECK(match::player(living).eliminated && match::player(living).missSequence == 10);
    CHECK(match::get().phase == Phase::Results && match::get().winner == 0);
    match::start_round();
    CHECK(match::get().round == 2 && !match::player(living).eliminated);
    // The host's own misses use the same authority and acknowledgements.
    const int ownHunter = hunter_id();
    g_fake.self = ownHunter; g_fake.host = ownHunter; match::on_host_changed(ownHunter);
    everyone_ready(4, map_info(match::get().map).stage); advance(21'000);
    match::report_miss();
    CHECK(match::my_hunter_life() == 18 && match::player(ownHunter).missSequence == 1);
}

static void test_search_clues() {
    std::printf("voluntary search clues and one final clue across arenas\n");
    CHECK(std::string(search_clue(10, 100, 1199).direction) == "Ahead");
    CHECK(std::string(search_clue(100, 10, 1200).direction) == "Right");
    CHECK(std::string(search_clue(-100, 10, 3500).direction) == "Left");
    CHECK(std::string(search_clue(10, -100, 2000).direction) == "Behind");
    CHECK(std::string(search_clue(10, 100, 1199).range) == "Near");
    CHECK(std::string(search_clue(10, 100, 1200).range) == "In the area");
    CHECK(std::string(search_clue(10, 100, 3500).range) == "Distant");
    CHECK(search_clue(0, 100, 0).arrowX == 0 && search_clue(0, 100, 0).arrowY == -1);
    CHECK(search_clue(100, 0, 0).arrowX == 1 && search_clue(100, 0, 0).arrowY == 0);
    CHECK(search_clue(-100, 0, 0).arrowX == -1 && search_clue(-100, 0, 0).arrowY == 0);
    CHECK(search_clue(0, -100, 0).arrowX == 0 && search_clue(0, -100, 0).arrowY == 1);
    CHECK(search_clue(0, 0, 0).arrowX == 0 && search_clue(0, 0, 0).arrowY == -1);
    for (int degrees = 0; degrees < 360; degrees += 15) {
        const float angle = degrees * 3.14159265f / 180;
        const auto clue = search_clue(std::sin(angle) * 500, std::cos(angle) * 500, 500);
        CHECK(std::fabs(clue.arrowX - std::sin(angle)) < 0.0001f);
        CHECK(std::fabs(clue.arrowY + std::cos(angle)) < 0.0001f);
        CHECK(std::fabs(std::hypot(clue.arrowX, clue.arrowY) - 1) < 0.0001f);
    }
    // A stationary prop moves around the indicator as the hunter turns the camera.
    const auto ahead = search_clue_from_view(0, -100, 0, -1, 100);
    const auto right = search_clue_from_view(0, -100, -1, 0, 100);
    const auto behind = search_clue_from_view(0, -100, 0, 1, 100);
    const auto left = search_clue_from_view(0, -100, 1, 0, 100);
    CHECK(ahead.arrowX == 0 && ahead.arrowY == -1);
    CHECK(right.arrowX == 1 && right.arrowY == 0);
    CHECK(behind.arrowX == 0 && behind.arrowY == 1);
    CHECK(left.arrowX == -1 && left.arrowY == 0);
    // Shallow bearings stay precise; moving across the prop reverses the indication.
    const auto diagonal = search_clue_from_view(30, -40, 0, -0.1f, 50);
    CHECK(std::fabs(diagonal.arrowX - 0.6f) < 0.0001f);
    CHECK(std::fabs(diagonal.arrowY + 0.8f) < 0.0001f);
    const auto passed = search_clue_from_view(-30, 40, 0, -1, 50);
    CHECK(std::fabs(passed.arrowX + diagonal.arrowX) < 0.0001f);
    CHECK(std::fabs(passed.arrowY + diagonal.arrowY) < 0.0001f);
    const auto verticalView = search_clue_from_view(30, -40, 0, 0, 50);
    CHECK(std::isfinite(verticalView.arrowX) && std::isfinite(verticalView.arrowY));
    CHECK(final_clue_window_ms(20, 180) == 20000 && final_clue_window_ms(60, 30) == 15000);
    { // finale on a compact arena and a large arena
        for (int map : {0, 9}) {
            host_room(4);
            auto rules = match::get().settings;
            rules.map = map; rules.seekSecs = 180;
            match::set_settings(rules); match::start_round();
            const char* stage = map_info(map).stage;
            everyone_ready(4, stage); advance(21000);
            CHECK(match::get().phase == Phase::Seek);
            const auto end = match::get().phaseEnd;
            const auto tick = [&](uint64_t at) {
                s_now = at;
                for (int id = 1; id <= 4; ++id)
                    deliver(id, state_msg(stage, id * 1000.0f, 0, 0));
                match::update();
            };
            g_fake.sent.clear();
            tick(end - 90000); // no movement and no regular/stationary reveal by default
            tick(end - 60000); // last minute does not accelerate clues
            tick(end - 20001);
            CHECK(count_sent(MSG_CLUE) == 0);
            tick(end - 20000);
            const int hiders = match::hiders_left();
            CHECK(count_sent(MSG_CLUE) == hiders);
            for (int id = 1; id <= 4; ++id) {
                if (match::player(id).role == Role::Hider)
                    CHECK(match::player(id).finalClueGiven && match::player(id).revealedUntil == s_now + kTauntRevealMs &&
                          match::player(id).finalRevealedUntil == s_now + kTauntRevealMs);
            }
            tick(end - 15000); tick(end - 10000);
            CHECK(count_sent(MSG_CLUE) == hiders);
            CHECK(match::next_clue_ms() == UINT32_MAX);
            // A promoted host carries the roster's flag and does not repeat the finale.
            g_fake.self = g_fake.host = 2; match::on_host_changed(2);
            tick(end - 5000);
            CHECK(count_sent(MSG_CLUE) == hiders);
            match::end_round(); match::start_round();
            for (int id = 1; id <= 4; ++id)
                CHECK(!match::player(id).finalClueGiven && match::player(id).finalRevealedUntil == 0);
        }
    }

    // A recent manual clue cannot postpone the exact reveal past the configured time mark.
    host_room(2);
    auto rules = match::get().settings; rules.seekSecs = 180;
    match::set_settings(rules); match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(2, stage); advance(21000);
    const auto end = match::get().phaseEnd;
    const int hider = hider_id();
    const auto fresh = [&] { deliver(hider, state_msg(stage, 0, 0, 0)); };
    s_now = end - 21000; fresh();
    Writer manual(MSG_TAUNT); manual.u32(match::get().round); manual.u8(0);
    deliver(hider, manual.bytes());
    g_fake.sent.clear();
    s_now = end - 20000; fresh(); match::update();
    CHECK(count_sent(MSG_CLUE) == 1 && match::player(hider).finalClueGiven);
    const auto finalUntil = match::player(hider).finalRevealedUntil;
    CHECK(finalUntil == s_now + kTauntRevealMs);
    g_fake.self = hider;
    CHECK(match::next_clue_ms() == UINT32_MAX);
    g_fake.self = g_fake.host;
    // Another pickup clue updates ordinary feedback without hiding or extending the exact pulse.
    s_now += 1000;
    Writer pickupClue(MSG_CLUE); pickupClue.u32(match::get().round); pickupClue.u8(hider);
    pickupClue.u8(5); pickupClue.u8(static_cast<uint8_t>(ClueKind::Treasure));
    deliver(g_fake.host, pickupClue.bytes());
    CHECK(match::player(hider).finalRevealedUntil == finalUntil);
    CHECK(match::player(hider).revealedUntil > finalUntil);
    s_now = end - 17000; fresh(); match::update();
    CHECK(count_sent(MSG_CLUE) == 1 && match::player(hider).finalClueGiven);

    // Off means quiet hiding for the entire hunt, but deliberate taunts remain available.
    host_room(2); rules = match::get().settings; rules.finalClueSecs = 0;
    match::set_settings(rules); match::start_round();
    stage = map_info(match::get().map).stage;
    everyone_ready(2, stage); advance(21000);
    s_now = match::get().phaseEnd - 1000;
    const int quiet = hider_id(); deliver(quiet, state_msg(stage, 0, 0, 0));
    g_fake.sent.clear(); match::update();
    CHECK(count_sent(MSG_CLUE) == 0);
    Writer voluntary(MSG_TAUNT); voluntary.u32(match::get().round); voluntary.u8(0);
    deliver(quiet, voluntary.bytes());
    CHECK(count_sent(MSG_CLUE) == 1);

    // Stationary pressure is still an explicit host option.
    host_room(2); rules = match::get().settings;
    rules.finalClueSecs = 0; rules.idleTauntSecs = 15;
    match::set_settings(rules); match::start_round();
    stage = map_info(match::get().map).stage;
    everyone_ready(2, stage); advance(21000);
    const int idle = hider_id(); deliver(idle, state_msg(stage, 0, 0, 0));
    g_fake.sent.clear(); advance(16000);
    deliver(idle, state_msg(stage, 0, 0, 0)); match::update();
    CHECK(count_sent(MSG_CLUE) == 1 && !match::player(idle).finalClueGiven);
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
    CHECK(!match::spawn_rupee(1000001, 0, 0));
    CHECK(match::spawn_rupee(10000, 0, 0)); // collision-validated host placements need no nearby player
    CHECK(match::spawn_rupee(700, 0, 0));
    CHECK(!match::spawn_rupee(1700, 0, 0)); // wider spacing, including different heights
    CHECK(!match::spawn_rupee(700, 10000, 0));
    auto item = match::get().rupees[1].id;
    request(hider, MSG_COLLECT_RUPEE, item); // too far
    CHECK(match::get().rupeeCount == 2 && match::player(hider).rupeesCollected == 0);
    fresh(hunter, 700); request(hunter, MSG_COLLECT_RUPEE, item);
    CHECK(match::get().rupeeCount == 2); // hunters cannot collect
    fresh(hider, 700); request(hider, MSG_COLLECT_RUPEE, item);
    CHECK(match::get().rupeeCount == 1 && match::player(hider).rupeesCollected == 1);
    CHECK(match::player(hider).roundPoints == 1);
    CHECK(match::player(hider).revealedUntil > s_now);
    request(hider, MSG_COLLECT_RUPEE, item);
    CHECK(match::player(hider).roundPoints == 1); // consumed, no duplicate reward
    for (int n = 0; n < 2; ++n) {
        const float x = 2100 + n * 1400;
        fresh(hider, x - 700);
        CHECK(match::spawn_rupee(x, 0, 0));
        item = match::get().rupees[1].id;
        fresh(hider, x); request(hider, MSG_COLLECT_RUPEE, item);
    }
    CHECK(match::player(hider).rupeesCollected == 3);
    CHECK(match::player(hider).roundPoints == 5); // three pickups and the challenge bonus
    advance(5000); fresh(hider, 2100);
    const auto points = match::player(hider).roundPoints;
    request(hider, MSG_TAUNT);
    CHECK(match::player(hider).roundPoints == points + 1); // manual taunts share the same bonus budget
    const auto reveal = match::player(hider).revealedUntil;
    request(hider, MSG_TAUNT);
    CHECK(match::player(hider).roundPoints == points + 1 && match::player(hider).revealedUntil == reveal);
    Writer forged(MSG_CLUE); forged.u32(match::get().round); forged.u8(hunter); forged.u8(0); forged.u8(0);
    deliver(static_cast<uint8_t>(hider == g_fake.host ? hunter : hider), forged.bytes());
    CHECK(match::player(hunter).revealedUntil == 0);
    advance(31000); fresh(hider, 2100); match::update();
    CHECK(match::player(hider).revealedUntil == reveal); // no recurring reveal after hiding quietly
    const auto before = match::player(hider).roundPoints;
    CHECK(before == points + 3); // manual bonus + two objective points, no automatic-taunt points
    CHECK(match::spawn_rupee(2800, 0, 0));
    item = match::get().rupees[1].id;
    Writer stale(MSG_COLLECT_RUPEE); stale.u32(match::get().round - 1); stale.u16(item);
    fresh(hider, 2800); deliver(static_cast<uint8_t>(hider), stale.bytes());
    CHECK(match::get().rupeeCount == 2);
    advance(match::kRupeeLifetimeMs + match::kRupeeRespawnCooldownMs + 100);
    CHECK(match::get().rupeeCount == 0);
    for (int n = 0; n < match::kMaxRupees; ++n) CHECK(match::spawn_rupee(n * 2000.0f, 0, 0));
    CHECK(match::get().rupeeCount == 24); // the host also decodes each full wire snapshot
    CHECK(!match::spawn_rupee(100000, 0, 0));
    match::end_round();
    CHECK(match::get().rupeeCount == 0);
}

static void test_treasure_respawn() {
    std::printf("treasure refills avoid recent pickups and stationary hiders\n");
    host_room(2);
    auto rules = match::get().settings;
    rules.seekSecs = 180;
    match::set_settings(rules);
    match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(2, stage);
    advance(21'000);
    const int hider = hider_id(), hunter = hunter_id();
    const auto move = [&](float x) {
        deliver(static_cast<uint8_t>(hider), state_msg(stage, x, 0, 0));
    };
    move(0);
    CHECK(!match::spawn_rupee(0, 0, 0));
    CHECK(!match::spawn_rupee(match::kRupeePlayerClearance - 1, 0, 0));
    CHECK(match::spawn_rupee(1200, 0, 0));
    move(1200);
    Writer pickup(MSG_COLLECT_RUPEE);
    pickup.u32(match::get().round); pickup.u16(match::get().rupees[0].id);
    deliver(static_cast<uint8_t>(hider), pickup.bytes());
    CHECK(match::get().rupeeCount == 0 && match::player(hider).rupeesCollected == 1);
    const auto collectedAt = s_now;
    move(-5000); // moving away must not instantly reopen the old location
    CHECK(!match::spawn_rupee(1200, 0, 0));
    CHECK(!match::spawn_rupee(1900, 0, 0));
    CHECK(!match::spawn_rupee(1200, 10000, 0));
    CHECK(match::spawn_rupee(5000, 0, 0)); // the rest of the map still refills

    // The farthest hole used to be selected on every refill, even with other valid ground.
    const std::vector<treasure::Point> points = {{1200,0,0}, {2400,0,0}, {5000,0,0}};
    const std::vector<treasure::Point> occupied = {{5000,0,0}};
    const auto allowed = [](treasure::Point p) { return !match::rupee_spawn_blocked(p.x, p.z); };
    CHECK(treasure::spread_candidate(points, occupied, 0, match::kRupeeSpacing, allowed) == 1);
    CHECK(treasure::spread_candidate({{1200,0,0}}, {}, 0, match::kRupeeSpacing, allowed) == 1);
    advance(5000);
    CHECK(!match::spawn_rupee(1200, 0, 0));
    s_now = collectedAt + match::kRupeeRespawnCooldownMs - 1;
    CHECK(!match::spawn_rupee(1200, 0, 0));
    ++s_now;
    move(1200);
    CHECK(!match::spawn_rupee(1200, 0, 0)); // waiting on the pickup cannot farm it
    CHECK(!match::spawn_rupee(1799, 0, 0));
    move(-5000);
    CHECK(match::spawn_rupee(1200, 0, 0)); // reusable once the cooldown ends and the hider leaves

    // A client records removals from snapshots and retains them when taking authority.
    g_fake.self = hider; g_fake.host = hunter;
    Writer empty(MSG_RUPEES); empty.u32(match::get().round); empty.u8(0);
    deliver(static_cast<uint8_t>(hider), empty.bytes());
    CHECK(match::get().rupeeCount == 2); // a non-host cannot consume loot or add cooldowns
    Writer malformed(MSG_RUPEES); malformed.u32(match::get().round); malformed.u8(1);
    deliver(static_cast<uint8_t>(hunter), malformed.bytes());
    CHECK(match::get().rupeeCount == 2);
    deliver(static_cast<uint8_t>(hunter), empty.bytes());
    CHECK(match::get().rupeeCount == 0);
    g_fake.host = hider;
    match::on_host_changed(static_cast<uint8_t>(hider));
    CHECK(!match::spawn_rupee(1200, 0, 0));
    CHECK(!match::spawn_rupee(5000, 0, 0));
    CHECK(match::spawn_rupee(8000, 0, 0));

    advance(match::kRupeeLifetimeMs + 100);
    CHECK(match::get().phase == Phase::Seek && match::get().rupeeCount == 0);
    CHECK(!match::spawn_rupee(8000, 0, 0)); // expiration also retires the location briefly
    CHECK(match::spawn_rupee(1200, 0, 0));

    // A new round announcement clears the client's old snapshot and cooldowns together.
    g_fake.host = hunter;
    Writer nextRound(MSG_ROUND);
    nextRound.u32(match::get().round + 1);
    nextRound.u8(match::get().map); nextRound.u16(rules.hideSecs); nextRound.u16(rules.seekSecs);
    nextRound.u8(1); nextRound.u8(0);
    deliver(static_cast<uint8_t>(hunter), nextRound.bytes());
    CHECK(match::get().rupeeCount == 0);
    deliver(static_cast<uint8_t>(hunter), empty.bytes()); // a previous-round removal is ignored
    g_fake.host = hider;
    match::on_host_changed(static_cast<uint8_t>(hider));
    CHECK(match::spawn_rupee(8000, 0, 0));

    match::end_round(); match::start_round();
    everyone_ready(2, stage); advance(21'000);
    CHECK(match::spawn_rupee(1200, 0, 0)); // cooldowns belong to their round
}

static void test_map_wide_treasure_layout() {
    std::printf("treasure covers distant ground and follows connected routes\n");
    using namespace hs::treasure;
    ReachableArea area(400);
    const auto openMap = [](Point, Point& at) {
        if (std::fabs(at.x) > 10000 || std::fabs(at.z) > 10000) return false;
        at.y = 0; return true;
    };
    area.seed({0,0,0}, openMap);
    CHECK(area.points().size() == 1);
    area.expand(2, openMap);
    CHECK(!area.complete() && area.points().size() < 100);
    while (!area.complete()) area.expand(32, openMap);
    CHECK(area.points().size() == 51 * 51);
    std::vector<Point> occupied;
    for (int n = 0; n < match::kMaxRupees; ++n) {
        const size_t index = spread_candidate(area.points(), occupied, 137 * n, match::kRupeeSpacing);
        CHECK(index < area.points().size());
        if (index == area.points().size()) break;
        const auto at = area.points()[index];
        CHECK(nearest_distance_sq(at, occupied) >= match::kRupeeSpacing * match::kRupeeSpacing);
        occupied.push_back(at);
    }
    int quadrants[4] = {};
    for (const auto& p : occupied) {
        ++quadrants[(p.x >= 0 ? 1 : 0) + (p.z >= 0 ? 2 : 0)];
    }
    for (int count : quadrants) CHECK(count >= 3); // players can remain at the centre
    CHECK(spread_candidate({}, occupied, 0, 1200) == 0);

    area.clear(200);
    // L-shaped ground turns behind a wall. The separate island has floor but no route.
    const auto onFloor = [](Point p) {
        return (p.x >= 0 && p.x <= 6000 && std::fabs(p.z) <= 400) ||
               (p.x >= 5600 && p.x <= 6400 && p.z >= 0 && p.z <= 6000) ||
               (p.x >= 0 && p.x <= 1000 && p.z >= 5000 && p.z <= 6000);
    };
    const auto path = [&](Point from, Point& at) {
        for (int i = 1; i <= 8; ++i) {
            const float t = i / 8.0f;
            if (!onFloor({from.x + (at.x - from.x) * t, 0, from.z + (at.z - from.z) * t})) return false;
        }
        at.y = 0; return true;
    };
    area.seed({0,0,0}, path);
    while (!area.complete()) area.expand(32, path);
    bool farCorner = false;
    for (const auto& p : area.points()) {
        CHECK(onFloor(p));
        CHECK(!(p.x < 1000 && p.z > 5000));
        if (p.x >= 5600 && p.z >= 5600) farCorner = true;
    }
    CHECK(farCorner);
    // A fresh player can seed a visited upper level without merging it with the floor below.
    const size_t lowerCount = area.points().size();
    area.seed({0,1000,0}, [](Point, Point&) { return true; });
    CHECK(area.points().size() == lowerCount + 1);
}

static void test_fair_round_scores() {
    std::printf("equal score budgets, shared finds, infection and bonus spending\n");
    for (const uint16_t seconds : {30, 60, 180, 900, 1800}) {
        CHECK(scoring::survival(seconds * 500u, seconds) == 6);
        CHECK(scoring::survival(seconds * 1000u + 100000, seconds) == 12);
    }
    for (int n = 1; n < kMaxPlayers; ++n) {
        CHECK(scoring::captures(n, n) == 12);
        CHECK(scoring::captures(n + 1, n) == 12);
    }
    for (int n = 2; n <= kMaxPlayers; ++n) {
        host_room(n);
        auto rules = match::get().settings;
        rules.hunters = std::min(2, n - 1);
        match::set_settings(rules);
        match::start_round();
        const char* stage = map_info(match::get().map).stage;
        everyone_ready(n, stage);
        while (match::get().phase != Phase::Seek) advance(1000);
        advance(5000);
        std::vector<int> hunters, hiders;
        for (int id = 1; id <= n; ++id) {
            (match::player(id).role == Role::Hunter ? hunters : hiders).push_back(id);
            deliver(static_cast<uint8_t>(id), state_msg(stage, 0, 0, 0, STATE_IN_WORLD | STATE_DISGUISED));
        }
        CHECK(match::get().startingHiders == hiders.size());
        for (size_t i = 0; i < hiders.size(); ++i) {
            // After the first find, an infected hider can help. Starting hunters still share
            // progress, and the infected player only receives their capped personal bonus.
            const int finder = i == 0 ? hunters[0] : hiders[0];
            deliver(static_cast<uint8_t>(finder), hit_msg(match::get().round, hiders[i]));
            CHECK(match::get().teamFinds == i + 1);
            for (const int hunter : hunters) CHECK(match::player(hunter).objectiveAwarded ==
                scoring::captures(i + 1, hiders.size()));
            deliver(static_cast<uint8_t>(finder), hit_msg(match::get().round, hiders[i]));
            CHECK(match::get().teamFinds == i + 1); // duplicate finds cannot earn more
        }
        advance(100);
        CHECK(match::get().winner == 1 && match::get().phase == Phase::Results);
        CHECK(match::player(hunters[0]).roundPoints == 18 + scoring::personal_find(hiders.size()) + (hiders.size() > 1 ? scoring::kFirstBloodPoints : 0));
        for (size_t i = 1; i < hunters.size(); ++i) CHECK(match::player(hunters[i]).roundPoints == 18);
        CHECK(match::player(hiders[0]).roundPoints <= scoring::kBonusPoints + scoring::survival(5000, rules.seekSecs)); // no survival or win award for their new team
        for (int id = 1; id <= n; ++id) CHECK(match::player(id).roundPoints <= scoring::kObjectivePoints + scoring::kBonusPoints + scoring::kWinPoints);
    }

    host_room(2);
    auto rules = match::get().settings;
    rules.seekSecs = 180; rules.freeDecoys = 0;
    match::set_settings(rules); match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(2, stage); advance(21'000);
    const int hider = hider_id();
    float lootX = 0;
    const auto loot = [&] {
        lootX += 2000;
        deliver(static_cast<uint8_t>(hider), state_msg(stage, lootX - 700, 0, 0, STATE_IN_WORLD | STATE_DISGUISED));
        CHECK(match::spawn_rupee(lootX, 0, 0));
        deliver(static_cast<uint8_t>(hider), state_msg(stage, lootX, 0, 0, STATE_IN_WORLD | STATE_DISGUISED));
        Writer pickup(MSG_COLLECT_RUPEE); pickup.u32(match::get().round); pickup.u16(match::get().rupees[0].id);
        deliver(static_cast<uint8_t>(hider), pickup.bytes());
    };
    for (int n = 0; n < 6; ++n) loot();
    CHECK(match::player(hider).bonusEarned == scoring::kBonusPoints && match::player(hider).roundPoints == scoring::kBonusPoints);
    deliver(static_cast<uint8_t>(hider), place_decoy_msg(match::get().round));
    CHECK(match::player(hider).roundPoints == scoring::kBonusPoints - match::kExtraDecoyCost);
    loot();
    CHECK(match::player(hider).roundPoints == scoring::kBonusPoints - match::kExtraDecoyCost && match::player(hider).bonusEarned == scoring::kBonusPoints);
    // A new host keeps the gross limit and the last survival award, even after spending.
    g_fake.host = hider; g_fake.self = hider;
    match::on_host_changed(hider);
    loot();
    CHECK(match::player(hider).roundPoints == scoring::kBonusPoints - match::kExtraDecoyCost && match::player(hider).bonusEarned == scoring::kBonusPoints);
    advance(181'000);
    CHECK(match::player(hider).roundPoints == scoring::kObjectivePoints + scoring::kWinPoints + scoring::kBonusPoints - match::kExtraDecoyCost); // 12 survival + 6 win + 3 unspent bonus
    match::start_round();
    CHECK(match::player(hider).bonusEarned == 0 && match::player(hider).objectiveAwarded == 0);
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
    CHECK(prop_on_map(kGoat, 1) && !prop_on_map(kGoat, 10));
    CHECK(prop_on_map(kCitizenFirst, 5) && !prop_on_map(kCitizenFirst, 1));
    CHECK(prop_on_map(kMapTable, 9) && !prop_on_map(kMapTable, 1));
    CHECK(!prop_on_map(kTreasureRupee, -1));

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
        for (float hunterX : {-500.0f, size[0] * 0.5f, size[0] + 500}) {
            for (float hunterY : {-500.0f, size[1] * 0.5f, size[1] + 500}) {
                const auto indicator = hud::clue_indicator(12, 24, size[0], size[1], hunterX, hunterY);
                const float margin = indicator.radius + 18;
                CHECK(indicator.radius >= 0 && indicator.radius <= 60);
                CHECK(indicator.x - margin >= 12 && indicator.x + margin <= 12 + size[0]);
                CHECK(indicator.y - margin >= 24 + 86);
                CHECK(indicator.y + margin <= 24 + size[1] - 78);
            }
        }
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

static void test_archive_bounds() {
    std::printf("private animation archive bounds and entry indices\n");
    std::vector<uint8_t> bytes(0x74);
    const auto word = [&](size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (24 - i * 8));
    };
    word(0, 0x52415243); word(4, bytes.size()); word(8, 0x20);
    word(12, 0x50); word(16, 4);
    word(0x28, 2); word(0x2C, 0x20);
    word(0x44, 0x02000000); // entry 0 is a directory
    word(0x54, 99u << 16);  // entry 1's file ID deliberately differs from its index
    word(0x58, 0x01000000); word(0x5C, 0); word(0x60, 4);
    bytes[0x70] = 'B'; bytes[0x71] = 'C'; bytes[0x72] = 'K'; bytes[0x73] = '!';
    const auto resource = rarc::resource_at(bytes, 1);
    CHECK(resource.size() == 4 && resource[0] == 'B' && resource[3] == '!');
    CHECK(rarc::resource_at(bytes, 0).empty());
    CHECK(rarc::resource_at(bytes, 99).empty());
    for (size_t length = 0; length < bytes.size(); ++length)
        CHECK(rarc::resource_at(std::span<const uint8_t>(bytes.data(), length), 1).empty());
    word(0x60, UINT32_MAX); CHECK(rarc::resource_at(bytes, 1).empty()); word(0x60, 4);
    word(0x5C, UINT32_MAX); CHECK(rarc::resource_at(bytes, 1).empty()); word(0x5C, 0);
    word(0x28, UINT32_MAX); CHECK(rarc::resource_at(bytes, 1).empty()); word(0x28, 2);
    word(0x2C, UINT32_MAX); CHECK(rarc::resource_at(bytes, 1).empty()); word(0x2C, 0x20);
    word(12, UINT32_MAX); CHECK(rarc::resource_at(bytes, 1).empty()); word(12, 0x50);
    word(8, UINT32_MAX); CHECK(rarc::resource_at(bytes, 1).empty()); word(8, 0x20);
    word(0, 0); CHECK(rarc::resource_at(bytes, 1).empty());
}

static void test_prop_abilities() {
    std::printf("authoritative swaps, limits and migration\n");
    host_room(3);
    auto rules = match::get().settings; rules.seekSecs = 300; match::set_settings(rules);
    match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(3, stage); advance(21'000);
    const int hunter = hunter_id(), hider = hider_id();
    const int other = 6 - hunter - hider;
    const auto state = [&](int id, float x) {
        deliver(id, state_msg(stage, x, 0, 0, STATE_IN_WORLD | STATE_DISGUISED, kPot, 1234));
    };
    state(hider, 0);
    deliver(hider, place_decoy_msg(match::get().round));
    CHECK(match::decoy_count() == 1);
    state(hider, 1000);
    Writer swap(MSG_SWAP); swap.u32(match::get().round);
    deliver(hunter, swap.bytes()); // hunters cannot teleport
    CHECK(match::player(hider).swapsUsed == 0);
    deliver(hider, swap.bytes());
    CHECK(match::player(hider).swapsUsed == 1 && match::player(hider).state.x == 0);
    CHECK(match::decoy(0).x == 1000 && match::player(hider).state.propYaw == 1234);
    CHECK(match::player(hider).previousStateAt == 0);
    CHECK(match::player(hider).swapReadyAt == s_now + match::kSwapCooldownMs);
    const auto teleports = count_sent(MSG_TELEPORT);
    deliver(hider, swap.bytes());
    CHECK(count_sent(MSG_TELEPORT) == teleports && match::player(hider).swapsUsed == 1);

    // A promoted peer retains the cooldown, even though the swap was sent to only its owner.
    g_fake.self = g_fake.host = other; match::on_host_changed(other);
    deliver(hider, swap.bytes()); CHECK(match::player(hider).swapsUsed == 1);
    advance(match::kSwapCooldownMs + 1); state(hider, -1000);
    deliver(hider, swap.bytes());
    CHECK(match::player(hider).swapsUsed == 2 && match::player(hider).state.x == 1000);
    advance(match::kSwapCooldownMs + 1); state(hider, 0);
    deliver(hider, swap.bytes()); CHECK(match::player(hider).swapsUsed == 2);
    Writer stale(MSG_SWAP); stale.u32(match::get().round - 1);
    deliver(hider, stale.bytes()); CHECK(match::player(hider).swapsUsed == 2);

    // The retired v11 whistle (message 30) is ignored, not answered.
    state(hunter, 200);
    Writer whistle(30); whistle.u32(match::get().round);
    g_fake.sent.clear();
    deliver(hunter, whistle.bytes()); CHECK(g_fake.sent.empty());
    CHECK(match::player(hider).revealedUntil == 0);

    // A late joiner receives the round's stats, using its own clock, and can become host.
    g_fake.add(4, "Late"); match::on_joined(4);
    std::vector<uint8_t> roster, round, phase;
    for (const auto& sent : g_fake.sent) {
        if (sent.bytes[0] == MSG_ROSTER) roster = sent.bytes;
        if (sent.bytes[0] == MSG_ROUND) round = sent.bytes;
        if (sent.bytes[0] == MSG_PHASE) phase = sent.bytes;
    }
    CHECK(roster.size() == 2 + 4 * 28);
    reset_net(4); g_fake.self = 4; g_fake.host = other; match::on_welcome();
    s_now += 100'000;
    deliver(other, roster); deliver(other, round); deliver(other, phase);
    CHECK(match::player(hider).swapsUsed == 2);
    g_fake.host = 4; match::on_host_changed(4); state(hider, 500);
    deliver(hider, swap.bytes()); CHECK(match::player(hider).swapsUsed == 2);

    // A partial roster cannot apply its first rows and leave the rest with older rules/stats.
    const auto points = match::player(hider).roundPoints;
    for (size_t length = 1; length < roster.size(); ++length) {
        match::on_message(4, roster.data(), length);
        CHECK(match::player(hider).roundPoints == points && match::player(hider).swapsUsed == 2);
    }
}

static void test_style_awards() {
    std::printf("risk rewards, decoy fools, close calls and quick finds\n");
    host_room(3); match::start_round();
    const char* stage = map_info(match::get().map).stage;
    everyone_ready(3, stage); advance(21'000); advance(5'000);
    const int hunter = hunter_id(), hider = hider_id(), other = 6 - hunter - hider;
    const auto fresh = [&] {
        deliver(hunter, state_msg(stage, 0, 0, 0));
        deliver(hider, state_msg(stage, 200, 0, 0, STATE_IN_WORLD | STATE_DISGUISED));
        deliver(other, state_msg(stage, 5000, 0, 0, STATE_IN_WORLD | STATE_DISGUISED));
    };
    fresh();
    Writer taunt(MSG_TAUNT); taunt.u32(match::get().round); taunt.u8(0);
    deliver(hider, taunt.bytes());
    CHECK(match::player(hider).bonusEarned == 2 && match::player(hider).taunts == 1);
    deliver(hider, taunt.bytes()); CHECK(match::player(hider).bonusEarned == 2);
    deliver(hunter, miss_msg(match::get().round, 1));
    CHECK(match::player(hider).closeCalls == 1 && match::player(hider).bonusEarned == 3);
    deliver(hunter, miss_msg(match::get().round, 1));
    deliver(hunter, miss_msg(match::get().round, 2));
    CHECK(match::player(hider).closeCalls == 1 && match::player(hunter).misses == 2);
    advance(scoring::kCloseCallGapMs + 1); fresh();
    deliver(hunter, miss_msg(match::get().round, 3));
    CHECK(match::player(hider).closeCalls == 2 && match::player(hider).bonusEarned == 4);
    deliver(hider, place_decoy_msg(match::get().round));
    CHECK(match::decoy_count() == 1);
    const auto decoy = match::decoy(0).id;
    deliver(hunter, hit_decoy_msg(match::get().round, decoy));
    CHECK(match::decoy_count() == 0 && match::player(hider).decoyFools == 1);
    CHECK(match::player(hider).bonusEarned == 5);
    deliver(hunter, hit_decoy_msg(match::get().round, decoy));
    CHECK(match::player(hider).bonusEarned == 5);
    deliver(hunter, hit_msg(match::get().round, hider));
    CHECK(match::player(hunter).bonusEarned == 4); // find + first blood
    deliver(other, state_msg(stage, 200, 0, 0));
    deliver(hunter, hit_msg(match::get().round, other)); advance(1);
    CHECK(match::get().winner == 1 && match::player(hunter).bonusEarned == 8);
    CHECK(match::player(hunter).roundPoints == 26);

    host_room(3); match::start_round();
    stage = map_info(match::get().map).stage;
    everyone_ready(3, stage); advance(21'000);
    const int finder = hunter_id(), first = hider_id(), survivor = 6 - finder - first;
    deliver(finder, state_msg(stage, 0, 0, 0)); deliver(first, state_msg(stage, 100, 0, 0));
    deliver(finder, hit_msg(match::get().round, first));
    advance(61'000);
    CHECK(match::get().winner == 0 && match::player(survivor).bonusEarned == 2);
    CHECK(match::player(survivor).roundPoints == 20);
}

int main() {
    test_archive_bounds();
    test_prop_abilities();
    test_style_awards();
    test_map_wide_treasure_layout();
    test_fair_round_scores();
    test_treasure_and_clues();
    test_treasure_respawn();
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
    test_hunter_elimination();
    test_find_healing();
    test_hunter_health_migration();
    test_search_clues();
    test_exit_collision();
    std::printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
