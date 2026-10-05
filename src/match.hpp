#pragma once

// The rules. The room's host runs them; everyone else applies what the host announces. The host's
// own announcements go through the same handlers, so every game follows one code path.

#include "common.hpp"
#include "protocol.hpp"

#include <string>
#include <vector>

namespace hs::match {

// Keep every player's full host-configurable allowance alive at once. The protocol uses byte-sized
// counts and ids, so 16 * 10 remains compact (a full snapshot is under 3 KiB) and leaves ids to
// spare for replacement after a decoy is struck.
constexpr int kMaxDecoysPerPlayer = 10;
constexpr int kMaxActiveDecoys = kMaxPlayers * kMaxDecoysPerPlayer;
constexpr int kExtraDecoyCost = 3;
constexpr int kMaxRupees = 24;
constexpr uint64_t kRupeeLifetimeMs = 90000;
constexpr float kRupeeSpacing = 1200.0f;
constexpr float kRupeeCollectRadius = 180.0f; // 100-unit local pickup plus one moving-state interval
constexpr uint64_t kRupeeRespawnCooldownMs = 30000;
constexpr float kRupeePlayerClearance = 600.0f;
constexpr uint64_t kSwapCooldownMs = 15000;
constexpr int kSwapsPerRound = 2;

struct Settings {
    uint8_t map = kRandomMap;
    uint16_t hideSecs = 30;
    uint16_t seekSecs = 180;
    uint8_t hunters = 0;  // 0 = map-aware, one per three/four players
    bool foundJoinHunters = true;
    uint8_t missPenaltyQuarters = 2;  // 0 = Off; 1..4 = quarter to one heart
    uint16_t finalClueSecs = 20;  // one clue per remaining hider; 0 disables it
    bool trackingPulse = true;
    bool treasure = true;
    uint16_t idleTauntSecs = 0;  // optional stationary clues; hiding still is safe by default
    bool autoNext = true;
    bool isPublic = false;
    uint8_t freeDecoys = 3;  // free placements per hider, available from the Hide phase
    bool decoySwap = true;   // hold D-pad up: trade places with your newest decoy

    void write(Writer& w) const;
    void read(Reader& r);
};

struct Player {
    bool present = false;
    Role role = Role::None;
    uint8_t color = 0;
    uint8_t wantColor = 0xFF;
    uint16_t score = 0;        // room total
    uint16_t roundPoints = 0;  // this round
    bool found = false;
    bool ready = false;
    uint8_t hunterRounds = 0;
    // Latest STATE from this player.
    PlayerState state;
    bool hasState = false;
    uint64_t stateAt = 0;
    PlayerState previousState;
    uint64_t previousStateAt = 0;
    uint64_t stageChangedAt = 0;  // tag immunity after loading a new area
    uint64_t lastTauntAt = 0;
    uint64_t lastClueAt = 0;
    uint64_t lastMovedAt = 0;
    uint64_t revealedUntil = 0;
    uint64_t finalRevealedUntil = 0; // separate so a pickup cannot hide or extend the exact reveal
    bool finalClueGiven = false;  // carried in the roster for late joins and host migration
    float idleX = 0, idleY = 0, idleZ = 0;
    uint8_t rupeesCollected = 0;
    uint8_t finds = 0;
    uint8_t hunterLife = 0;   // quarters, owned by the host; only confirmed finds restore life
    bool eliminated = false;
    uint16_t missSequence = 0; // cumulative misses processed by the host, carried across migration
    Role startingRole = Role::None; // unchanged by infection; late joiners have no win/progress award
    uint8_t bonusEarned = 0;    // gross bonuses, so spending cannot reopen the farming allowance
    uint64_t lastDecoyAt = 0;
    uint16_t objectiveAwarded = 0;  // host bookkeeping for survival/capture progress
    uint8_t decoysUsed = 0;
    // Round stats, carried in the roster for the results awards.
    uint8_t decoyFools = 0;   // hunters who struck this player's decoys
    uint8_t closeCalls = 0;   // missed swings right next to this hider
    uint8_t taunts = 0;
    uint8_t swapsUsed = 0;
    uint8_t misses = 0;       // a hunter's missed swings (decoys included)
    uint64_t swapReadyAt = 0;     // local-clock deadline, carried as remaining time in the roster
    uint64_t quickFindUntil = 0;
    uint64_t closeCallReadyAt = 0;
};

struct Decoy {
    uint8_t id = 0;
    uint8_t owner = 0;
    uint8_t prop = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int16_t yaw = 0;
};

struct Match {
    Settings settings;
    Phase phase = Phase::Lobby;
    uint32_t round = 0;
    uint8_t map = 0;  // this round's map (never kRandomMap)
    uint64_t phaseEnd = 0;
    int winner = -1;  // last round: 0 props, 1 hunters
    Player players[kSlots];
    Decoy decoys[kMaxActiveDecoys];
    uint8_t decoyCount = 0;
    struct Rupee {
        uint16_t id = 0;
        float x = 0, y = 0, z = 0;
        uint64_t expiresAt = 0;
    } rupees[kMaxRupees];
    uint8_t rupeeCount = 0;
    uint8_t startingHiders = 0;
    uint8_t teamFinds = 0;
};

// Short messages for the HUD feed and big centre banners.
struct Notice {
    std::string text;
    uint8_t r = 255, g = 255, b = 255;
    bool big = false;
    uint64_t at = 0;
};

const Match& get();
const Player& player(int id);
Role my_role();
bool in_round();  // Gather, Hide or Seek
uint32_t ms_left();
int hiders_left();
int count_role(Role role);
int decoy_count();
const Decoy& decoy(int index);
int my_decoys_left();  // remaining free placements; extra placements cost kExtraDecoyCost
bool can_place_decoy();
bool can_swap();         // decoy swap available right now
int my_swaps_left();
uint32_t next_clue_ms();
int rupee_points();
bool rupee_spawn_blocked(float x, float z); // recent pickups and fresh, active hiders
bool spawn_rupee(float x, float y, float z); // host: reachable position checked by treasure module
void collect_rupee(uint16_t id);

// Presentation reads these; each Notice is shown for a few seconds.
const std::vector<Notice>& notices();

// UI actions. Host only unless noted.
void set_settings(const Settings& s);
bool can_start(std::string* why);
void start_round();
void end_round();
void reset_scores();
void set_wanted_color(uint8_t color);  // anyone

// Local reports (anyone).
void set_local_state(const PlayerState& s);  // every frame; sent at 10 Hz
void report_ready();
void report_hit(uint8_t target);
void report_miss();
uint16_t my_hunter_life();  // includes unacknowledged local misses for immediate feedback
void place_decoy();
void request_swap();
void report_decoy_hit(uint8_t decoyId);
void send_taunt(uint8_t sound);

#ifdef HS_LAB
// Test harness only: a host-side decoy at an exact spot, for side-by-side disguise checks.
void lab_add_decoy(uint8_t prop, float x, float y, float z, int16_t yaw);
void lab_clear_decoys();
#endif

// Wiring to net.
void on_welcome();
void on_joined(uint8_t id);
void on_left(uint8_t id);
void on_host_changed(uint8_t id);
void on_message(uint8_t from, const uint8_t* data, size_t size);
void on_disconnected();

// Every frame.
void update();

// Hooks for the local-player module: taunts from others and confirmed swaps.
struct Hooks {
    void (*taunt)(uint8_t from, uint8_t sound, ClueKind kind) = nullptr;
    void (*teleport)(float x, float y, float z, int16_t yaw) = nullptr;
};
void set_hooks(const Hooks& hooks);

}  // namespace hs::match
