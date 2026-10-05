#include "match.hpp"

#include "maps.hpp"
#include "net.hpp"
#include "props.hpp"
#include "gameplay.hpp"
#include "scoring.hpp"

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
constexpr uint64_t kDecoyCooldownMs = 750;
constexpr uint64_t kFreshStateMs = 2000;
constexpr float kMinDecoySpacing = 100.0f;
constexpr uint64_t kTauntPointEveryMs = 10000;
constexpr size_t kMaxNotices = 6;

Match s_match;
std::vector<Notice> s_notices;
Hooks s_hooks;
uint64_t s_lastStateSent = 0;
PlayerState s_lastWireState;
bool s_haveWireState = false;
uint64_t s_lastMeta = 0;
uint64_t s_seekStartedAt = 0;  // host clock, for survival points
uint64_t s_lastHitSent[kSlots] = {};
uint64_t s_lastDecoyHitSent[kMaxActiveDecoys + 1] = {};
uint8_t s_nextDecoyId = 1;
uint16_t s_nextRupeeId = 1;
uint64_t s_lastPickupSent = 0;
uint16_t s_missSequence = 0;
struct RupeeCooldown { float x, z; uint64_t until; };
std::vector<RupeeCooldown> s_rupeeCooldowns;
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

uint16_t remaining(uint64_t until) {
    const auto now = now_ms();
    return static_cast<uint16_t>(std::min<uint64_t>(65535, until > now ? until - now : 0));
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
        w.u8(static_cast<uint8_t>((p.found ? 1 : 0) | (p.ready ? 2 : 0) |
            (p.startingRole == Role::Hunter ? 4 : 0) | (p.startingRole == Role::Hider ? 8 : 0) |
            (p.finalClueGiven ? 16 : 0) | (p.eliminated ? 32 : 0)));
        w.u8(p.hunterRounds);
        w.u8(p.decoysUsed);
        w.u8(p.rupeesCollected);
        w.u8(p.finds);
        w.u8(p.bonusEarned);
        w.u8(static_cast<uint8_t>(p.objectiveAwarded));
        w.u8(p.hunterLife);
        w.u16(p.missSequence);
        w.u8(p.decoyFools);
        w.u8(p.closeCalls);
        w.u8(p.taunts);
        w.u8(p.swapsUsed);
        w.u8(p.misses);
        w.u16(remaining(p.swapReadyAt));
        w.u16(remaining(p.quickFindUntil));
        w.u16(remaining(p.closeCallReadyAt));
    }
    return w;
}

Writer decoys_msg() {
    Writer w(MSG_DECOYS);
    w.u32(s_match.round);
    w.u8(s_match.decoyCount);
    for (int i = 0; i < s_match.decoyCount; ++i) {
        const Decoy& d = s_match.decoys[i];
        w.u8(d.id);
        w.u8(d.owner);
        w.u8(d.prop);
        w.f32(d.x);
        w.f32(d.y);
        w.f32(d.z);
        w.s16(d.yaw);
    }
    return w;
}

Writer round_msg() {
    Writer w(MSG_ROUND);
    w.u32(s_match.round);
    w.u8(s_match.map);
    w.u16(s_match.settings.hideSecs);
    w.u16(s_match.settings.seekSecs);
    w.u8(s_match.startingHiders);
    w.u8(s_match.teamFinds);
    return w;
}

Writer rupees_msg() {
    Writer w(MSG_RUPEES);
    w.u32(s_match.round);
    w.u8(s_match.rupeeCount);
    for (int i = 0; i < s_match.rupeeCount; ++i) {
        const auto& r = s_match.rupees[i];
        w.u16(r.id);
        w.f32(r.x); w.f32(r.y); w.f32(r.z);
        w.u32(static_cast<uint32_t>(r.expiresAt > now_ms() ? r.expiresAt - now_ms() : 0));
    }
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
    net::send_meta(s.isPublic, label, 0, s.map == kRandomMap ? 255 : s.map,
        static_cast<int>(s_match.phase));
    s_lastMeta = now_ms();
}

void go_phase(Phase phase, uint32_t ms) {
    // Set on reception too; our announcement follows the same path as every other client.
    const Phase previous = s_match.phase;
    s_match.phase = phase;
    s_match.phaseEnd = now_ms() + ms;
    if (phase == Phase::Seek) s_seekStartedAt = now_ms();
    const Writer transition = phase_msg();
    s_match.phase = previous;
    announce(transition);
    if (phase == Phase::Seek && previous != Phase::Seek) {
        for (auto& p : s_match.players) p.lastClueAt = p.lastMovedAt = now_ms();
    }
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
    return scoring::survival(until - s_seekStartedAt, s_match.settings.seekSecs);
}

void give(int id, int points) {
    Player& p = P(id);
    p.roundPoints = static_cast<uint16_t>(std::min(65535, p.roundPoints + points));
    p.score = static_cast<uint16_t>(std::min(65535, p.score + points));
}

int give_bonus(int id, int requested) {
    Player& p = P(id);
    const int points = scoring::bonus(p.bonusEarned, requested);
    p.bonusEarned += points;
    give(id, points);
    return points;
}

bool award_objective(int id, int earned) {
    Player& p = P(id);
    if (earned <= p.objectiveAwarded) return false;
    give(id, earned - p.objectiveAwarded);
    p.objectiveAwarded = earned;
    return true;
}

bool fresh_on_map(const Player& p) {
    return p.present && p.hasState && now_ms() - p.stateAt <= kFreshStateMs &&
           (p.state.flags & STATE_IN_WORLD) &&
           std::strncmp(p.state.stage, map_info(s_match.map).stage, 8) == 0;
}

float horizontal_distance(const PlayerState& a, float x, float z) {
    return std::hypot(a.x - x, a.z - z);
}

// The nearest live, fresh hunter on the round's map, or a negative distance when there is none.
float nearest_hunter(float x, float z) {
    float best = -1.0f;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const Player& h = P(id);
        if (h.role != Role::Hunter || h.eliminated || !fresh_on_map(h)) continue;
        const float d = horizontal_distance(h.state, x, z);
        if (best < 0.0f || d < best) best = d;
    }
    return best;
}

// Bonus points with feedback for everyone. Zero points are still announced, so a player at the
// bonus limit sees why nothing was added.
int award(int id, Award kind, int requested) {
    const int points = give_bonus(id, requested);
    Writer w(MSG_AWARD);
    w.u32(s_match.round); w.u8(static_cast<uint8_t>(id)); w.u8(static_cast<uint8_t>(kind));
    w.u8(static_cast<uint8_t>(points));
    announce(w);
    return points;
}

void host_clue(int id, uint8_t sound, ClueKind kind) {
    Player& p = P(id);
    const uint64_t now = now_ms();
    if (s_match.phase != Phase::Seek || p.role != Role::Hider || p.found || !fresh_on_map(p) ||
        (kind != ClueKind::Treasure && kind != ClueKind::Final && p.lastClueAt != 0 &&
         now - p.lastClueAt < kTauntCooldownMs)) return;
    // Only deliberate taunts earn points. Automatic clues cannot farm a hiding spot. A taunt with
    // a hunter close by is worth more: the reward follows the risk.
    const bool scored = kind == ClueKind::Manual &&
                        (p.lastTauntAt == 0 || now - p.lastTauntAt >= kTauntPointEveryMs);
    Writer w(MSG_CLUE);
    w.u32(s_match.round); w.u8(static_cast<uint8_t>(id)); w.u8(sound); w.u8(static_cast<uint8_t>(kind));
    announce(w);
    if (kind == ClueKind::Manual) p.taunts = static_cast<uint8_t>(std::min(255, p.taunts + 1));
    if (scored) {
        p.lastTauntAt = now;
        const float hunter = nearest_hunter(p.state.x, p.state.z);
        if (hunter >= 0.0f && hunter <= scoring::kBoldTauntRange) {
            award(id, Award::BoldTaunt, scoring::kBoldTauntPoints);
        } else {
            give_bonus(id, scoring::kTauntPoints);
        }
    }
    if (kind == ClueKind::Manual) announce(roster_msg());
}

void clear_rupees() {
    s_match.rupeeCount = 0;
    for (auto& r : s_match.rupees) r = {};
}

void remember_rupee(const Match::Rupee& r) {
    const auto now = now_ms();
    std::erase_if(s_rupeeCooldowns, [now](const RupeeCooldown& c) { return c.until <= now; });
    s_rupeeCooldowns.push_back({r.x, r.z, now + kRupeeRespawnCooldownMs});
}

void erase_rupee(int index) {
    remember_rupee(s_match.rupees[index]);
    for (int i = index + 1; i < s_match.rupeeCount; ++i) s_match.rupees[i - 1] = s_match.rupees[i];
    s_match.rupees[--s_match.rupeeCount] = {};
}

void host_collect(int id, uint16_t pickupId) {
    Player& p = P(id);
    if (!s_match.settings.treasure || s_match.phase != Phase::Seek || p.role != Role::Hider ||
        p.found || !fresh_on_map(p)) return;
    for (int i = 0; i < s_match.rupeeCount; ++i) {
        const auto& r = s_match.rupees[i];
        if (r.id != pickupId || r.expiresAt <= now_ms()) continue;
        const float dx = p.state.x - r.x, dy = p.state.y - r.y, dz = p.state.z - r.z;
        if (dx * dx + dz * dz > kRupeeCollectRadius * kRupeeCollectRadius || std::fabs(dy) > 140.0f) return;
        erase_rupee(i); // consume before announcing; simultaneous requests can only award once
        p.rupeesCollected = static_cast<uint8_t>(std::min(255, p.rupeesCollected + 1));
        const bool bonus = p.rupeesCollected == 3;
        const int points = give_bonus(id, rupee_points() + (bonus ? 2 : 0));
        announce(rupees_msg());
        announce(roster_msg());
        Writer w(MSG_PICKUP);
        w.u32(s_match.round); w.u8(static_cast<uint8_t>(id)); w.u8(static_cast<uint8_t>(points)); w.u8(bonus);
        announce(w);
        host_clue(id, 5, ClueKind::Treasure);
        return;
    }
}

bool award_survival(int id, uint64_t until) {
    return award_objective(id, survival_points(until));
}

void clear_decoys() {
    s_match.decoyCount = 0;
    for (Decoy& d : s_match.decoys) d = Decoy{};
}

void erase_decoy(int index) {
    if (index < 0 || index >= s_match.decoyCount) return;
    for (int i = index + 1; i < s_match.decoyCount; ++i) {
        s_match.decoys[i - 1] = s_match.decoys[i];
    }
    --s_match.decoyCount;
    s_match.decoys[s_match.decoyCount] = Decoy{};
}

int decoy_index(uint8_t id) {
    for (int i = 0; i < s_match.decoyCount; ++i) {
        if (s_match.decoys[i].id == id) return i;
    }
    return -1;
}

uint8_t take_decoy_id() {
    for (int tries = 0; tries < 255; ++tries) {
        const uint8_t id = s_nextDecoyId++;
        if (s_nextDecoyId == 0) s_nextDecoyId = 1;
        if (id != 0 && decoy_index(id) < 0) return id;
    }
    return 0;
}

void host_place_decoy(int owner) {
    Player& p = P(owner);
    const uint64_t now = now_ms();
    if ((s_match.phase != Phase::Hide && s_match.phase != Phase::Seek) || !p.present ||
        p.role != Role::Hider || p.found || !p.hasState || now - p.stateAt > kFreshStateMs ||
        !(p.state.flags & STATE_IN_WORLD) || !(p.state.flags & STATE_DISGUISED) ||
        std::strncmp(p.state.stage, map_info(s_match.map).stage, 8) != 0 ||
        !prop_on_map(p.state.prop, s_match.map) || now - p.lastDecoyAt < kDecoyCooldownMs ||
        !std::isfinite(p.state.x) || !std::isfinite(p.state.y) || !std::isfinite(p.state.z)) {
        return;
    }

    const bool free = p.decoysUsed < s_match.settings.freeDecoys;
    if (!free && (s_match.phase != Phase::Seek || p.roundPoints < kExtraDecoyCost)) return;
    for (int i = 0; i < s_match.decoyCount; ++i) {
        const Decoy& d = s_match.decoys[i];
        const float dx = d.x - p.state.x, dy = d.y - p.state.y, dz = d.z - p.state.z;
        if (dx * dx + dy * dy + dz * dz < kMinDecoySpacing * kMinDecoySpacing) return;
    }

    int owned = 0;
    int oldestOwned = -1;
    for (int i = 0; i < s_match.decoyCount; ++i) {
        if (s_match.decoys[i].owner == owner) {
            if (oldestOwned < 0) oldestOwned = i;
            ++owned;
        }
    }
    // Paid Hunt placements may extend a player's initial allowance, but keep the live actor and
    // wire state bounded. Replacing only that player's oldest decoy means a busy room can never
    // erase somebody else's setup.
    if (owned >= kMaxDecoysPerPlayer) erase_decoy(oldestOwned);
    if (s_match.decoyCount >= kMaxActiveDecoys) erase_decoy(0);

    const uint8_t id = take_decoy_id();
    if (id == 0) return;
    if (!free) {
        p.roundPoints = static_cast<uint16_t>(p.roundPoints - kExtraDecoyCost);
        p.score = static_cast<uint16_t>(p.score >= kExtraDecoyCost ? p.score - kExtraDecoyCost : 0);
    }
    p.decoysUsed = static_cast<uint8_t>(std::min(255, static_cast<int>(p.decoysUsed) + 1));
    p.lastDecoyAt = now;
    Decoy& d = s_match.decoys[s_match.decoyCount++];
    d.id = id;
    d.owner = static_cast<uint8_t>(owner);
    d.prop = p.state.prop;
    d.x = p.state.x;
    d.y = p.state.y;
    d.z = p.state.z;
    d.yaw = p.state.propYaw;
    announce(decoys_msg());
    announce(roster_msg());
}

void host_hit_decoy(int attacker, uint8_t id) {
    if (s_match.phase != Phase::Seek) return;
    Player& p = P(attacker);
    const int index = decoy_index(id);
    if (index < 0 || !p.present || p.role != Role::Hunter || !p.hasState ||
        now_ms() - p.stateAt > kFreshStateMs || !(p.state.flags & STATE_IN_WORLD) ||
        std::strncmp(p.state.stage, map_info(s_match.map).stage, 8) != 0) {
        return;
    }
    const Decoy& d = s_match.decoys[index];
    const float dx = p.state.x - d.x, dy = p.state.y - d.y, dz = p.state.z - d.z;
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > kMaxHitDistance) return;
    const int owner = d.owner;
    erase_decoy(index);
    announce(decoys_msg());
    // The decoy did its job: its hider earns a little for every hunter it fooled.
    Player& o = P(owner);
    if (o.present && o.role == Role::Hider && !o.found && o.decoyFools < scoring::kMaxDecoyFools) {
        o.decoyFools = static_cast<uint8_t>(o.decoyFools + 1);
        award(owner, Award::DecoyFooled, scoring::kDecoyFoolPoints);
        announce(roster_msg());
    }
}

// Trade places with your newest decoy: you appear where it stood, and it takes your old spot.
void host_swap(int id) {
    Player& p = P(id);
    const uint64_t now = now_ms();
    if (!s_match.settings.decoySwap || s_match.phase != Phase::Seek || p.role != Role::Hider ||
        p.found || !fresh_on_map(p) || !(p.state.flags & STATE_DISGUISED) ||
        p.swapsUsed >= kSwapsPerRound || now < p.swapReadyAt) {
        return;
    }
    int newest = -1;
    for (int i = 0; i < s_match.decoyCount; ++i) {
        if (s_match.decoys[i].owner == id) newest = i;
    }
    if (newest < 0) return;
    Decoy& d = s_match.decoys[newest];
    const float x = d.x, y = d.y, z = d.z;
    const int16_t yaw = d.yaw;
    d.x = p.state.x; d.y = p.state.y; d.z = p.state.z;
    d.yaw = p.state.propYaw;
    d.prop = p.state.prop;
    p.swapsUsed = static_cast<uint8_t>(p.swapsUsed + 1);
    p.swapReadyAt = now + kSwapCooldownMs;
    // Move the host's view of the hider now, so a hit claim in flight is judged at the new spot.
    p.state.x = x; p.state.y = y; p.state.z = z;
    p.state.yaw = p.state.propYaw = yaw;
    p.previousStateAt = 0;
    p.idleX = x; p.idleY = y; p.idleZ = z;
    p.lastMovedAt = now;
    announce(decoys_msg());
    Writer w(MSG_TELEPORT);
    w.u32(s_match.round); w.f32(x); w.f32(y); w.f32(z); w.s16(yaw);
    if (id == self()) apply(self(), w.bytes());
    else net::send(static_cast<uint8_t>(id), w.bytes());
    announce(roster_msg());
}

void finish(int winner) {
    const uint64_t now = now_ms();
    int survivors = 0;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const Player& p = P(id);
        survivors += p.present && p.role == Role::Hider && !p.found ? 1 : 0;
    }
    for (int id = 1; id <= kMaxPlayers; ++id) {
        Player& p = P(id);
        if (p.present && p.role == Role::Hider && !p.found) {
            award_survival(id, now);
            if (winner == 0) give(id, scoring::kWinPoints);
            // Outlasting every other prop deserves a mention, but only in a real hunt.
            if (winner == 0 && survivors == 1 && s_match.startingHiders > 1 &&
                s_match.phase == Phase::Seek && now >= s_match.phaseEnd) {
                award(id, Award::LastStanding, scoring::kLastStandingPoints);
            }
        } else if (p.present && p.startingRole == Role::Hunter && winner == 1) {
            give(id, scoring::kWinPoints);
        }
    }
    clear_decoys();
    clear_rupees();
    announce(rupees_msg());
    announce(decoys_msg());
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
    t.hunterLife = t.role == Role::Hunter ? kArenaLife : 0;
    award_survival(target, now_ms());
    const uint64_t now = now_ms();
    Player& hunter = P(by);
    give_bonus(by, scoring::personal_find(s_match.startingHiders));
    if (s_match.teamFinds == 0 && s_match.startingHiders > 1) {
        award(by, Award::FirstBlood, scoring::kFirstBloodPoints);
    } else if (hunter.quickFindUntil != 0 && now <= hunter.quickFindUntil) {
        award(by, Award::QuickFind, scoring::kQuickFindPoints);
    }
    hunter.quickFindUntil = now + scoring::kQuickFindMs;
    P(by).finds = static_cast<uint8_t>(std::min(255, P(by).finds + 1));
    P(by).hunterLife = static_cast<uint8_t>(std::min<int>(kArenaLife, P(by).hunterLife + 4));
    ++s_match.teamFinds;
    const int progress = scoring::captures(s_match.teamFinds, s_match.startingHiders);
    for (int id = 1; id <= kMaxPlayers; ++id) {
        if (P(id).present && P(id).startingRole == Role::Hunter) award_objective(id, progress);
    }
    Writer w(MSG_FOUND);
    w.u32(s_match.round);
    w.u8(static_cast<uint8_t>(target));
    w.u8(static_cast<uint8_t>(by));
    w.u8(s_match.teamFinds);
    announce(w);
    announce(roster_msg());
}

void host_miss(int id, uint16_t sequence) {
    if (s_match.phase != Phase::Seek) return;
    Player& p = P(id);
    if (!p.present || p.role != Role::Hunter || sequence <= p.missSequence || sequence > 4096) return;
    // A cumulative counter makes duplicate reports harmless and preserves pending misses when
    // the host changes. A player can only report their own misses, never a life/healing value.
    const unsigned misses = sequence - p.missSequence;
    p.missSequence = sequence;
    p.misses = static_cast<uint8_t>(std::min<unsigned>(255, p.misses + misses));
    p.hunterLife = static_cast<uint8_t>(life_after_miss(p.hunterLife,
        static_cast<uint8_t>(std::min<unsigned>(kArenaLife, misses * s_match.settings.missPenaltyQuarters))));
    // The closest prop held its nerve while the sword swung past it.
    if (fresh_on_map(p)) {
        int nearest = 0;
        float best = scoring::kCloseCallRange;
        for (int other = 1; other <= kMaxPlayers; ++other) {
            const Player& h = P(other);
            if (h.role != Role::Hider || h.found || !fresh_on_map(h)) continue;
            const float d = std::hypot(h.state.x - p.state.x, h.state.z - p.state.z);
            if (d <= best && std::fabs(h.state.y - p.state.y) < 250.0f) { best = d; nearest = other; }
        }
        Player& h = P(nearest);
        const uint64_t now = now_ms();
        if (nearest != 0 && h.closeCalls < scoring::kMaxCloseCalls &&
            now >= h.closeCallReadyAt) {
            h.closeCalls = static_cast<uint8_t>(h.closeCalls + 1);
            h.closeCallReadyAt = now + scoring::kCloseCallGapMs;
            award(nearest, Award::CloseCall, scoring::kCloseCallPoints);
        }
    }
    if (p.hunterLife == 0) {
        p.eliminated = true;
        p.role = Role::Spectator;
        Writer w(MSG_HUNTER_OUT); w.u32(s_match.round); w.u8(static_cast<uint8_t>(id));
        announce(w);
    }
    announce(roster_msg());
    if (count_role(Role::Hunter) == 0 && count_role(Role::Hider) != 0) finish(0);
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
    if (!fresh_on_map(a) || !fresh_on_map(t)) return;
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
        {
            bool changed = false;
            bool pickupsChanged = false;
            for (int i = s_match.rupeeCount - 1; i >= 0; --i) {
                if (s_match.rupees[i].expiresAt <= now) { erase_rupee(i); pickupsChanged = true; }
            }
            if (pickupsChanged) announce(rupees_msg());
            for (int id = 1; id <= kMaxPlayers; ++id) {
                const Player& p = P(id);
                if (p.present && p.role == Role::Hider && !p.found) {
                    changed = award_survival(id, now) || changed;
                    const auto& rules = s_match.settings;
                    if (now < s_match.phaseEnd && rules.finalClueSecs != 0 && !p.finalClueGiven &&
                        ms_left() <= final_clue_window_ms(rules.finalClueSecs, rules.seekSecs)) {
                        host_clue(id, static_cast<uint8_t>(s_rng() % 6), ClueKind::Final);
                        changed = p.finalClueGiven || changed;
                    } else if (s_match.settings.idleTauntSecs != 0 &&
                        now - std::max(p.lastMovedAt, p.lastClueAt) >= s_match.settings.idleTauntSecs * 1000u) {
                        host_clue(id, static_cast<uint8_t>(s_rng() % 6), ClueKind::Stationary);
                    }
                }
            }
            if (changed) announce(roster_msg());
        }
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
    if (!r.ok() || n > kMaxPlayers) return;
    Player incoming[kSlots];
    std::copy(std::begin(s_match.players), std::end(s_match.players), std::begin(incoming));
    bool listed[kSlots] = {};
    const uint64_t now = now_ms();
    for (int i = 0; i < n; ++i) {
        const uint8_t id = r.u8();
        const Role role = static_cast<Role>(r.u8());
        const uint8_t color = r.u8();
        const uint16_t score = r.u16();
        const uint16_t roundPoints = r.u16();
        const uint8_t flags = r.u8();
        const uint8_t hunterRounds = r.u8();
        const uint8_t decoysUsed = r.u8();
        const uint8_t rupeesCollected = r.u8();
        const uint8_t finds = r.u8();
        const uint8_t bonusEarned = r.u8();
        const uint8_t objectiveAwarded = r.u8();
        const uint8_t hunterLife = r.u8();
        const uint16_t missSequence = r.u16();
        const uint8_t decoyFools = r.u8();
        const uint8_t closeCalls = r.u8();
        const uint8_t taunts = r.u8();
        const uint8_t swapsUsed = r.u8();
        const uint8_t misses = r.u8();
        const uint16_t swapLeft = r.u16();
        const uint16_t findLeft = r.u16(), closeCallLeft = r.u16();
        if (!r.ok() || id < 1 || id > kMaxPlayers || listed[id] ||
            role > Role::Spectator) return;
        Player& p = incoming[id];
        listed[id] = true;
        p.present = true;
        p.role = role;
        p.color = color < kMaxPlayers ? color : 0;
        p.score = score;
        p.roundPoints = roundPoints;
        p.found = (flags & 1) != 0;
        p.ready = (flags & 2) != 0;
        p.finalClueGiven = (flags & 16) != 0;
        p.eliminated = (flags & 32) != 0;
        p.hunterLife = std::min<uint8_t>(hunterLife, kArenaLife);
        p.missSequence = missSequence;
        p.hunterRounds = hunterRounds;
        p.decoysUsed = decoysUsed;
        p.rupeesCollected = rupeesCollected;
        p.finds = finds;
        p.startingRole = (flags & 4) ? Role::Hunter : (flags & 8) ? Role::Hider : Role::None;
        p.bonusEarned = std::min<uint8_t>(bonusEarned, scoring::kBonusPoints);
        p.objectiveAwarded = std::min<uint8_t>(objectiveAwarded, scoring::kObjectivePoints);
        p.decoyFools = decoyFools;
        p.closeCalls = closeCalls;
        p.taunts = taunts;
        p.swapsUsed = std::min<uint8_t>(swapsUsed, kSwapsPerRound);
        p.misses = misses;
        p.swapReadyAt = swapLeft ? now + std::min<uint64_t>(swapLeft, kSwapCooldownMs) : 0;
        p.quickFindUntil = findLeft ? now + std::min<uint64_t>(findLeft, scoring::kQuickFindMs) : 0;
        p.closeCallReadyAt = closeCallLeft ? now + std::min<uint64_t>(closeCallLeft, scoring::kCloseCallGapMs) : 0;
    }
    if (r.ok()) std::copy(std::begin(incoming), std::end(incoming), std::begin(s_match.players));
}

bool valid_state(const PlayerState& s) {
    if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z) ||
        std::fabs(s.x) > 1000000 || std::fabs(s.y) > 1000000 || std::fabs(s.z) > 1000000) return false;
    for (const auto& a : s.under) if (!std::isfinite(a.frame)) return false;
    for (const auto& a : s.upper) if (!std::isfinite(a.frame)) return false;
    return true;
}

void remember_state(Player& p, const PlayerState& s) {
    const bool stageChanged = !p.hasState || std::strcmp(p.state.stage, s.stage) != 0;
    const uint64_t now = now_ms();
    if (stageChanged) p.stageChangedAt = now;
    const float dx = s.x - p.idleX, dz = s.z - p.idleZ;
    if (stageChanged || dx * dx + dz * dz > 120.0f * 120.0f || std::fabs(s.y - p.idleY) > 80.0f) {
        p.idleX = s.x; p.idleY = s.y; p.idleZ = s.z; p.lastMovedAt = now;
    }
    if (!stageChanged && p.stateAt != now) { p.previousState = p.state; p.previousStateAt = p.stateAt; }
    if (stageChanged) p.previousStateAt = 0;
    p.state = s;
    p.hasState = true;
    p.stateAt = now;
}

void handle_state(uint8_t from, Reader& r) {
    PlayerState s;
    s.read(r);
    if (!r.ok() || !valid_state(s)) return;
    remember_state(P(from), s);
}

}  // namespace

// ---- Settings ---------------------------------------------------------------------------------

void Settings::write(Writer& w) const {
    w.u8(map);
    w.u16(hideSecs);
    w.u16(seekSecs);
    w.u8(hunters);
    w.u8(static_cast<uint8_t>((foundJoinHunters ? 1 : 0) | (decoySwap ? 2 : 0) |
                              (autoNext ? 8 : 0) | (isPublic ? 16 : 0) | (trackingPulse ? 32 : 0) |
                              (treasure ? 64 : 0)));
    w.u16(idleTauntSecs);
    w.u8(freeDecoys);
    w.u8(missPenaltyQuarters);
    w.u16(finalClueSecs);
}

void Settings::read(Reader& r) {
    const uint8_t wantedMap = r.u8();
    map = wantedMap == kRandomMap || wantedMap < map_count() ? wantedMap : kRandomMap;
    hideSecs = std::clamp<uint16_t>(r.u16(), 10, 600);
    seekSecs = std::clamp<uint16_t>(r.u16(), 30, 1800);
    hunters = r.u8();
    const uint8_t f = r.u8();
    foundJoinHunters = f & 1;
    decoySwap = f & 2;
    autoNext = f & 8;
    isPublic = f & 16;
    trackingPulse = f & 32;
    treasure = f & 64;
    idleTauntSecs = std::clamp<uint16_t>(r.u16(), 0, 600);
    freeDecoys = std::clamp<uint8_t>(r.u8(), 0, 10);
    missPenaltyQuarters = std::min<uint8_t>(r.u8(), 4);
    finalClueSecs = std::min<uint16_t>(r.u16(), 60);
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

int decoy_count() {
    return s_match.decoyCount;
}

const Decoy& decoy(int index) {
    static Decoy none;
    return index >= 0 && index < s_match.decoyCount ? s_match.decoys[index] : none;
}

int my_decoys_left() {
    const Player& me = P(self());
    return std::max(0, static_cast<int>(s_match.settings.freeDecoys) - me.decoysUsed);
}

bool can_place_decoy() {
    const Player& me = P(self());
    const uint64_t now = now_ms();
    if ((s_match.phase != Phase::Hide && s_match.phase != Phase::Seek) || !me.present ||
        me.role != Role::Hider || me.found || !me.hasState || now - me.stateAt > kFreshStateMs ||
        !(me.state.flags & STATE_IN_WORLD) || !(me.state.flags & STATE_DISGUISED) ||
        std::strncmp(me.state.stage, map_info(s_match.map).stage, 8) != 0 ||
        !prop_on_map(me.state.prop, s_match.map) || !std::isfinite(me.state.x) ||
        !std::isfinite(me.state.y) || !std::isfinite(me.state.z)) {
        return false;
    }
    // Mirror the host's spacing rule so D-pad feedback is truthful. The host still repeats every
    // check because a modified or lagged client must never be authoritative.
    for (int i = 0; i < s_match.decoyCount; ++i) {
        const Decoy& d = s_match.decoys[i];
        const float dx = d.x - me.state.x, dy = d.y - me.state.y, dz = d.z - me.state.z;
        if (dx * dx + dy * dy + dz * dz < kMinDecoySpacing * kMinDecoySpacing) return false;
    }
    return my_decoys_left() > 0 ||
           (s_match.phase == Phase::Seek && me.roundPoints >= kExtraDecoyCost);
}

int my_swaps_left() {
    if (!s_match.settings.decoySwap) return 0;
    return std::max(0, kSwapsPerRound - static_cast<int>(P(self()).swapsUsed));
}

bool can_swap() {
    const Player& me = P(self());
    const uint64_t now = now_ms();
    if (!s_match.settings.decoySwap || s_match.phase != Phase::Seek || me.role != Role::Hider ||
        me.found || my_swaps_left() == 0 || !(me.state.flags & STATE_DISGUISED) ||
        now < me.swapReadyAt) return false;
    for (int i = 0; i < s_match.decoyCount; ++i) {
        if (s_match.decoys[i].owner == self()) return true;
    }
    return false;
}

uint32_t next_clue_ms() {
    const auto& p = P(self());
    if (s_match.phase != Phase::Seek || p.role != Role::Hider || p.found) return UINT32_MAX;
    uint64_t next = UINT64_MAX;
    if (s_match.settings.finalClueSecs != 0 && !p.finalClueGiven) {
        next = s_match.phaseEnd - final_clue_window_ms(s_match.settings.finalClueSecs,
                                                      s_match.settings.seekSecs);
    }
    if (s_match.settings.idleTauntSecs != 0) next = std::min(next,
        std::max(p.lastMovedAt, p.lastClueAt) + s_match.settings.idleTauntSecs * 1000u);
    if (next == UINT64_MAX) return UINT32_MAX;
    return static_cast<uint32_t>(next > now_ms() ? next - now_ms() : 0);
}

int rupee_points() { return s_match.round % 3 == 0 ? 2 : 1; }

bool rupee_spawn_blocked(float x, float z) {
    const auto now = now_ms();
    for (const auto& c : s_rupeeCooldowns) {
        const float dx = x - c.x, dz = z - c.z;
        if (c.until > now && dx * dx + dz * dz < kRupeeSpacing * kRupeeSpacing) return true;
    }
    // Even after a cooldown ends, a stationary hider must move to reach the next pickup.
    for (const auto& p : s_match.players) {
        if (p.role != Role::Hider || p.found || !fresh_on_map(p)) continue;
        const float dx = x - p.state.x, dz = z - p.state.z;
        if (dx * dx + dz * dz < kRupeePlayerClearance * kRupeePlayerClearance) return true;
    }
    return false;
}

bool spawn_rupee(float x, float y, float z) {
    if (!host() || !s_match.settings.treasure || s_match.phase != Phase::Seek ||
        s_match.rupeeCount >= kMaxRupees || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    for (int i = 0; i < s_match.rupeeCount; ++i) {
        const auto& r = s_match.rupees[i];
        const float dx = x - r.x, dz = z - r.z;
        if (dx * dx + dz * dz < kRupeeSpacing * kRupeeSpacing) return false;
    }
    if (std::fabs(x) > 1000000 || std::fabs(y) > 1000000 || std::fabs(z) > 1000000) return false;
    if (rupee_spawn_blocked(x, z)) return false;
    auto& r = s_match.rupees[s_match.rupeeCount++];
    r = {s_nextRupeeId++, x, y, z, now_ms() + kRupeeLifetimeMs};
    if (s_nextRupeeId == 0) s_nextRupeeId = 1;
    announce(rupees_msg());
    return true;
}

void collect_rupee(uint16_t id) {
    if (now_ms() - s_lastPickupSent < 350) return;
    s_lastPickupSent = now_ms();
    if (host()) { host_collect(self(), id); return; }
    Writer w(MSG_COLLECT_RUPEE); w.u32(s_match.round); w.u16(id);
    net::send(net::kToHost, w.bytes());
}

const std::vector<Notice>& notices() {
    return s_notices;
}

// ---- actions ---------------------------------------------------------------------------------

void set_settings(const Settings& s) {
    if (!host() || in_round()) return;
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
    if (in_round()) {
        if (why) *why = "A round is already in progress.";
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
    s_match.map = s_match.settings.map == kRandomMap ? static_cast<uint8_t>(random_map(s_match.map, n))
                                                     : s_match.settings.map;
    int hunters = s_match.settings.hunters != 0 ? s_match.settings.hunters
                                                : recommended_hunters(n, s_match.map);
    hunters = std::clamp(hunters, 1, n - 1);

    // Fewest turns as hunter first, ties broken at random.
    std::shuffle(ids.begin(), ids.end(), s_rng);
    std::stable_sort(ids.begin(), ids.end(),
        [](int a, int b) { return P(a).hunterRounds < P(b).hunterRounds; });
    for (int i = 0; i < n; ++i) {
        Player& p = P(ids[i]);
        p.role = i < hunters ? Role::Hunter : Role::Hider;
        p.startingRole = p.role;
        if (p.role == Role::Hunter) ++p.hunterRounds;
        p.found = false;
        p.ready = false;
        p.roundPoints = 0;
        p.lastDecoyAt = 0;
        p.objectiveAwarded = 0;
        p.bonusEarned = 0;
        p.decoysUsed = 0;
        p.rupeesCollected = 0;
        p.finds = 0;
        p.hunterLife = p.role == Role::Hunter ? kArenaLife : 0;
        p.eliminated = false;
        p.missSequence = 0;
        p.finalClueGiven = false;
        p.finalRevealedUntil = 0;
        p.lastTauntAt = p.lastClueAt = p.lastMovedAt = p.revealedUntil = 0;
        p.decoyFools = p.closeCalls = p.taunts = p.swapsUsed = p.misses = 0;
        p.swapReadyAt = p.quickFindUntil = p.closeCallReadyAt = 0;
    }

    s_match.round += 1;
    s_missSequence = 0;
    s_match.winner = -1;
    s_match.startingHiders = static_cast<uint8_t>(n - hunters);
    s_match.teamFinds = 0;
    s_seekStartedAt = 0;
    s_nextDecoyId = 1;
    s_nextRupeeId = 1;
    s_rupeeCooldowns.clear();
    clear_decoys();
    clear_rupees();
    announce(roster_msg());
    announce(round_msg());
    announce(decoys_msg());
    announce(rupees_msg());
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
    if (net::status() != net::Status::Online || !valid_state(s)) return;
    Player& me = P(self());
    remember_state(me, s);
    const uint64_t now = now_ms();
    const bool changed = !s_haveWireState || s.flags != s_lastWireState.flags ||
        std::strcmp(s.stage, s_lastWireState.stage) != 0 || s.room != s_lastWireState.room ||
        s.prop != s_lastWireState.prop || std::fabs(s.x - s_lastWireState.x) > 1.0f ||
        std::fabs(s.y - s_lastWireState.y) > 1.0f || std::fabs(s.z - s_lastWireState.z) > 1.0f ||
        s.yaw != s_lastWireState.yaw || s.propYaw != s_lastWireState.propYaw;
    const uint64_t interval = changed ? kStateIntervalMs : (in_round() ? 500 : 1000);
    if (s_haveWireState && now - s_lastStateSent < interval) return;
    s_lastStateSent = now;
    s_lastWireState = s;
    s_haveWireState = true;
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
    if (my_role() != Role::Hunter || my_hunter_life() == 0) return;
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

uint16_t my_hunter_life() {
    const Player& me = P(self());
    const unsigned pending = s_missSequence > me.missSequence ? s_missSequence - me.missSequence : 0;
    return life_after_miss(me.hunterLife, static_cast<uint8_t>(std::min<unsigned>(kArenaLife,
        pending * s_match.settings.missPenaltyQuarters)));
}

void report_miss() {
    // Misses are reported even without a heart penalty: they also score close calls and stats.
    if (net::status() != net::Status::Online || s_match.phase != Phase::Seek ||
        my_role() != Role::Hunter || my_hunter_life() == 0) return;
    s_missSequence = static_cast<uint16_t>(std::max(s_missSequence, P(self()).missSequence) + 1);
    if (host()) host_miss(self(), s_missSequence);
    else {
        Writer w(MSG_MISS); w.u32(s_match.round); w.u16(s_missSequence);
        net::send(net::kToHost, w.bytes());
    }
}

void place_decoy() {
    if (!can_place_decoy()) return;
    if (host()) {
        host_place_decoy(self());
        return;
    }
    Writer w(MSG_PLACE_DECOY);
    w.u32(s_match.round);
    net::send(net::kToHost, w.bytes());
}

void request_swap() {
    if (!can_swap()) return;
    // Optimistic cooldown so a held button cannot queue several requests.
    if (host()) {
        host_swap(self());
        return;
    }
    P(self()).swapReadyAt = now_ms() + kSwapCooldownMs;
    Writer w(MSG_SWAP);
    w.u32(s_match.round);
    net::send(net::kToHost, w.bytes());
}

void report_decoy_hit(uint8_t decoyId) {
    if (decoyId == 0 || my_role() != Role::Hunter || my_hunter_life() == 0) return;
    const uint64_t now = now_ms();
    const int throttle = decoyId % (kMaxActiveDecoys + 1);
    if (now - s_lastDecoyHitSent[throttle] < 500) return;
    s_lastDecoyHitSent[throttle] = now;
    if (host()) {
        host_hit_decoy(self(), decoyId);
        return;
    }
    Writer w(MSG_HIT_DECOY);
    w.u32(s_match.round);
    w.u8(decoyId);
    net::send(net::kToHost, w.bytes());
}

void send_taunt(uint8_t sound) {
    Writer w(MSG_TAUNT);
    w.u32(s_match.round); w.u8(sound);
    if (host()) host_clue(self(), sound, ClueKind::Manual);
    else net::send(net::kToHost, w.bytes());
}

// ---- net wiring ------------------------------------------------------------------------------

void on_welcome() {
    s_match = Match{};
    s_rupeeCooldowns.clear();
    s_haveWireState = false;
    s_lastStateSent = s_lastPickupSent = 0;
    s_missSequence = 0;
    s_notices.clear();
    for (int id = 1; id <= kMaxPlayers; ++id) {
        P(id).present = net::member(id).present;
        P(id).color = static_cast<uint8_t>((id - 1) % kMaxPlayers);
    }
    if (host()) {
        assign_color(self());
        announce(settings_msg());
        announce(roster_msg());
        announce(decoys_msg());
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
    p.hunterLife = p.role == Role::Hunter ? kArenaLife : 0;
    const Writer settings = settings_msg();
    net::send(id, settings.bytes());
    announce(roster_msg());
    if (in_round() || s_match.phase == Phase::Results) {
        net::send(id, round_msg().bytes());
        net::send(id, phase_msg().bytes());
        net::send(id, decoys_msg().bytes());
        net::send(id, rupees_msg().bytes());
    }
    send_meta();
}

void on_left(uint8_t id) {
    notice(std::string(name_of(id)) + " left", -1, false);
    P(id).present = false;
    P(id).hasState = false;
    if (host()) {
        bool removed = false;
        for (int i = s_match.decoyCount - 1; i >= 0; --i) {
            if (s_match.decoys[i].owner == id) {
                erase_decoy(i);
                removed = true;
            }
        }
        if (removed) announce(decoys_msg());
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
        for (int i = s_match.decoyCount - 1; i >= 0; --i) {
            if (!P(s_match.decoys[i].owner).present) erase_decoy(i);
        }
        s_nextDecoyId = 1;
        while (decoy_index(s_nextDecoyId) >= 0 && s_nextDecoyId != 0) ++s_nextDecoyId;
        if (s_nextDecoyId == 0) s_nextDecoyId = 1;
        s_nextRupeeId = 1;
        for (int i = 0; i < s_match.rupeeCount; ++i)
            s_nextRupeeId = std::max<uint16_t>(s_nextRupeeId, s_match.rupees[i].id + 1);
        if (s_nextRupeeId == 0) s_nextRupeeId = 1;
        // Objective and gross bonus counters arrive in the roster, including points already
        // spent on decoys. Carry them across host changes without granting duplicate awards.
        announce(roster_msg());
        announce(decoys_msg());
        announce(rupees_msg());
        send_meta();
    }
    if (s_match.phase == Phase::Seek && my_role() == Role::Hunter &&
        s_missSequence > P(self()).missSequence) {
        if (host()) host_miss(self(), s_missSequence);
        else {
            Writer w(MSG_MISS); w.u32(s_match.round); w.u16(s_missSequence);
            net::send(net::kToHost, w.bytes());
        }
    }
}

void on_disconnected() {
    s_match = Match{};
    s_missSequence = 0;
    s_rupeeCooldowns.clear();
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

    case MSG_SETTINGS: {
        if (!fromHost) break;
        Settings incoming;
        incoming.read(r);
        if (r.ok()) s_match.settings = incoming;
        break;
    }

    case MSG_ROSTER:
        if (fromHost) handle_roster(r);
        break;

    case MSG_DECOYS: {
        if (!fromHost) break;
        const uint32_t round = r.u32();
        const uint8_t count = r.u8();
        Decoy incoming[kMaxActiveDecoys];
        bool ids[256] = {};
        bool valid = true;
        if (count > kMaxActiveDecoys) break;
        for (int i = 0; i < count && r.ok(); ++i) {
            Decoy& d = incoming[i];
            d.id = r.u8();
            d.owner = r.u8();
            d.prop = r.u8();
            d.x = r.f32();
            d.y = r.f32();
            d.z = r.f32();
            d.yaw = r.s16();
            if (d.id == 0 || ids[d.id] || d.owner < 1 || d.owner > kMaxPlayers ||
                !prop_on_map(d.prop, -1) || !std::isfinite(d.x) || !std::isfinite(d.y) ||
                !std::isfinite(d.z)) {
                valid = false;
            }
            ids[d.id] = true;
        }
        if (!r.ok() || !valid || round != s_match.round) break;
        clear_decoys();
        s_match.decoyCount = count;
        for (int i = 0; i < count; ++i) s_match.decoys[i] = incoming[i];
        break;
    }

    case MSG_RUPEES: {
        if (!fromHost) break;
        const uint32_t round = r.u32();
        const uint8_t count = r.u8();
        if (count > kMaxRupees) break;
        Match::Rupee incoming[kMaxRupees];
        bool valid = true;
        for (int i = 0; i < count; ++i) {
            auto& p = incoming[i];
            p.id = r.u16(); p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
            const uint32_t lifetime = r.u32(); p.expiresAt = now_ms() + lifetime;
            if (p.id == 0 || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                lifetime > kRupeeLifetimeMs) valid = false;
            for (int j = 0; j < i; ++j) if (incoming[j].id == p.id) valid = false;
        }
        if (!r.ok() || !valid || round != s_match.round) break;
        // Every client remembers removed locations, so becoming host cannot reopen a farm.
        // The current protocol already supplies the old and new snapshots we need.
        if (s_match.phase == Phase::Seek) {
            for (int i = 0; i < s_match.rupeeCount; ++i) {
                const auto& old = s_match.rupees[i];
                bool remains = false;
                for (int j = 0; j < count; ++j) if (incoming[j].id == old.id) remains = true;
                if (!remains) remember_rupee(old);
            }
        }
        clear_rupees(); s_match.rupeeCount = count;
        for (int i = 0; i < count; ++i) s_match.rupees[i] = incoming[i];
        break;
    }

    case MSG_CLUE: {
        if (!fromHost) break;
        const uint32_t round = r.u32(); const uint8_t id = r.u8(); const uint8_t sound = r.u8();
        const uint8_t kind = r.u8();
        if (!r.ok() || round != s_match.round || s_match.phase != Phase::Seek ||
            id < 1 || id > kMaxPlayers || kind > static_cast<uint8_t>(ClueKind::Treasure) ||
            P(id).role != Role::Hider || P(id).found) break;
        P(id).lastClueAt = now_ms(); P(id).revealedUntil = now_ms() + kTauntRevealMs;
        if (kind == static_cast<uint8_t>(ClueKind::Final)) {
            P(id).finalClueGiven = true;
            P(id).finalRevealedUntil = now_ms() + kTauntRevealMs;
        }
        if (s_hooks.taunt) s_hooks.taunt(id, sound, static_cast<ClueKind>(kind));
        break;
    }

    case MSG_PICKUP: {
        if (!fromHost) break;
        const uint32_t round = r.u32(); const uint8_t id = r.u8();
        const uint8_t points = r.u8(); const bool bonus = r.u8() != 0;
        if (!r.ok() || round != s_match.round || id < 1 || id > kMaxPlayers) break;
        if (id == self()) {
            big(points == 0 ? "Treasure collected! Bonus limit reached." :
                "+" + std::to_string(points) + (bonus ? "! Treasure challenge complete!" : " treasure points!"), 120, 255, 145);
        } else if (bonus) notice(std::string(name_of(id)) + " completed the treasure challenge", P(id).color);
        break;
    }

    case MSG_ROUND: {
        if (!fromHost) break;
        const uint32_t round = r.u32();
        const uint8_t map = r.u8();
        const uint16_t hide = r.u16();
        const uint16_t seek = r.u16();
        const uint8_t startingHiders = r.u8();
        const uint8_t teamFinds = r.u8();
        if (!r.ok() || map >= map_count() || startingHiders >= kMaxPlayers ||
            teamFinds > startingHiders) break;
        if (round != s_match.round) {
            s_missSequence = P(self()).missSequence;
            s_rupeeCooldowns.clear();
            clear_rupees();
        }
        s_match.round = round;
        s_match.map = map;
        s_match.settings.hideSecs = hide;
        s_match.settings.seekSecs = seek;
        s_match.winner = -1;
        s_match.startingHiders = startingHiders;
        s_match.teamFinds = teamFinds;
        s_seekStartedAt = 0;
        const char* role = my_role() == Role::Hunter ? "You are a HUNTER" : "You are a PROP";
        big(std::string("Round ") + std::to_string(round) + ": " + map_info(map).name, 255, 230, 140);
        notice(role, P(self()).color, false);
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
        if (phase == Phase::Seek && s_seekStartedAt == 0) {
            const uint32_t duration = s_match.settings.seekSecs * 1000u;
            s_seekStartedAt = now_ms() - (duration - std::min(left, duration));
        }
        if (phase == before) break;
        if (phase == Phase::Seek) {
            for (auto& p : s_match.players) p.lastClueAt = p.lastMovedAt = now_ms();
            if (s_match.settings.treasure && s_match.round % 3 == 0) notice("TREASURE RUSH: rupees are worth 2 points!", -1);
        }
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
        const uint32_t round = r.u32();
        const uint8_t target = r.u8();
        const uint8_t by = r.u8();
        const uint8_t teamFinds = r.u8();
        if (!r.ok() || round != s_match.round || target < 1 || target > kMaxPlayers ||
            by < 1 || by > kMaxPlayers || teamFinds > s_match.startingHiders) break;
        s_match.teamFinds = teamFinds;
        Player& t = P(target);
        t.found = true;
        std::string what = t.hasState ? std::string(" (") + prop_info(t.state.prop).name + ")"
                                      : std::string();
        notice(std::string(name_of(by)) + " found " + name_of(target) + what, P(by).color, false);
        if (target == self()) {
            big("You were found!", 255, 120, 90);
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
            big("PROPS WIN!", 120, 230, 120);
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

    case MSG_MISS: {
        const uint32_t round = r.u32(); const uint16_t sequence = r.u16();
        if (r.ok() && host() && round == s_match.round) host_miss(from, sequence);
        break;
    }

    case MSG_HUNTER_OUT: {
        const uint32_t round = r.u32(); const uint8_t id = r.u8();
        if (!r.ok() || !fromHost || round != s_match.round || id < 1 || id > kMaxPlayers) break;
        P(id).eliminated = true;
        P(id).hunterLife = 0;
        P(id).role = Role::Spectator;
        notice(std::string(name_of(id)) + " ran out of hearts", P(id).color);
        if (id == self()) big("OUT OF HEARTS!", 255, 120, 90);
        break;
    }

    case MSG_PLACE_DECOY: {
        const uint32_t round = r.u32();
        if (!r.ok() || !host() || round != s_match.round) break;
        host_place_decoy(from);
        break;
    }

    case MSG_HIT_DECOY: {
        const uint32_t round = r.u32();
        const uint8_t id = r.u8();
        if (!r.ok() || !host() || round != s_match.round) break;
        host_hit_decoy(from, id);
        break;
    }

    case MSG_TAUNT: {
        const uint32_t round = r.u32();
        const uint8_t sound = r.u8();
        if (!r.ok() || !host() || round != s_match.round) break;
        host_clue(from, sound, ClueKind::Manual);
        break;
    }

    case MSG_COLLECT_RUPEE: {
        const uint32_t round = r.u32(); const uint16_t id = r.u16();
        if (r.ok() && host() && round == s_match.round) host_collect(from, id);
        break;
    }

    case MSG_SWAP: {
        const uint32_t round = r.u32();
        if (r.ok() && host() && round == s_match.round) host_swap(from);
        break;
    }

    case MSG_TELEPORT: {
        const uint32_t round = r.u32();
        const float x = r.f32(), y = r.f32(), z = r.f32();
        const int16_t yaw = r.s16();
        if (!r.ok() || !fromHost || round != s_match.round || !std::isfinite(x) ||
            !std::isfinite(y) || !std::isfinite(z)) break;
        P(self()).swapReadyAt = now_ms() + kSwapCooldownMs;
        if (s_hooks.teleport) s_hooks.teleport(x, y, z, yaw);
        big("SWAPPED!", 140, 220, 255);
        break;
    }

    case MSG_AWARD: {
        const uint32_t round = r.u32();
        const uint8_t id = r.u8(), kind = r.u8(), points = r.u8();
        if (!r.ok() || !fromHost || round != s_match.round || id < 1 || id > kMaxPlayers ||
            kind >= static_cast<uint8_t>(Award::Count)) break;
        static constexpr const char* kNames[] = {"Decoy fooled a hunter", "Close call",
            "Bold taunt", "First find", "Quick find", "Last prop standing"};
        const std::string text = std::string(kNames[kind]) +
            (points ? "  +" + std::to_string(points) : std::string("  (bonus limit)"));
        if (id == self()) big(text + "!", 255, 220, 120);
        else if (kind == static_cast<uint8_t>(Award::LastStanding) ||
                 kind == static_cast<uint8_t>(Award::FirstBlood)) {
            notice(std::string(name_of(id)) + ": " + kNames[kind], P(id).color);
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

#ifdef HS_LAB
void lab_add_decoy(uint8_t prop, float x, float y, float z, int16_t yaw) {
    if (!host() || s_match.decoyCount >= kMaxActiveDecoys) return;
    const uint8_t id = take_decoy_id();
    if (id == 0) return;
    Decoy& d = s_match.decoys[s_match.decoyCount++];
    d = {id, self(), prop, x, y, z, yaw};
    announce(decoys_msg());
}

void lab_clear_decoys() {
    if (!host()) return;
    clear_decoys();
    announce(decoys_msg());
}
#endif

}  // namespace hs::match
