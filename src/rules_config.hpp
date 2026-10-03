#pragma once
#include "maps.hpp"
#include "match.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace hs::settings {

// ---- current format (rules version 6) --------------------------------------------------------
// "map,hide,seek,hunters,flags,idle-seconds,free-decoys,miss-quarters,final-seconds"
// flags: 1 found hiders hunt, 2 decoy swap, 4 hunter whistle, 8 automatic rounds, 16 public,
//        32 hunter tracking, 64 treasure.

inline match::Settings parse_rules(const std::string& text) {
    match::Settings s;
    int v[9] = {};
    const int fields = std::sscanf(text.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2],
        &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]);
    if (fields < 9) return s;
    s.map = v[0] >= 0 && v[0] < map_count() ? static_cast<uint8_t>(v[0]) : kRandomMap;
    s.hideSecs = static_cast<uint16_t>(std::clamp(v[1], 10, 600));
    s.seekSecs = static_cast<uint16_t>(std::clamp(v[2], 30, 1800));
    s.hunters = static_cast<uint8_t>(std::clamp(v[3], 0, 8));
    s.foundJoinHunters = v[4] & 1;
    s.decoySwap = v[4] & 2;
    s.whistle = v[4] & 4;
    s.autoNext = v[4] & 8;
    s.isPublic = v[4] & 16;
    s.trackingPulse = v[4] & 32;
    s.treasure = v[4] & 64;
    s.idleTauntSecs = static_cast<uint16_t>(std::clamp(v[5], 0, 600));
    s.freeDecoys = static_cast<uint8_t>(std::clamp(v[6], 0, 10));
    s.missPenaltyQuarters = static_cast<uint8_t>(std::clamp(v[7], 0, 4));
    s.finalClueSecs = static_cast<uint16_t>(std::clamp(v[8], 0, 60));
    return s;
}

inline std::string format_rules(const match::Settings& s) {
    const int flags = (s.foundJoinHunters ? 1 : 0) | (s.decoySwap ? 2 : 0) | (s.whistle ? 4 : 0) |
                      (s.autoNext ? 8 : 0) | (s.isPublic ? 16 : 0) | (s.trackingPulse ? 32 : 0) |
                      (s.treasure ? 64 : 0);
    char text[80];
    std::snprintf(text, sizeof(text), "%d,%u,%u,%u,%d,%u,%u,%u,%u", s.map, s.hideSecs, s.seekSecs,
        s.hunters, flags, s.idleTauntSecs, s.freeDecoys, s.missPenaltyQuarters, s.finalClueSecs);
    return text;
}

// ---- legacy format (rules versions 1-5, mod v0.3.x) -----------------------------------------
// "mode,map,hide,seek,hunters,flags,idle,free-decoys,miss-quarters,final-seconds", where the
// flags were 1 join, 2 miss penalty, 4 final clue, 8 auto, 16 public, 32 tracking, 64 treasure.
// The map list changed in v0.4, so a legacy map choice becomes Random.

inline match::Settings parse_legacy_rules(const std::string& text) {
    match::Settings s;
    int v[10] = {};
    const int fields = std::sscanf(text.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2],
        &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
    if (fields < 6) return s;
    s.map = v[1] >= 0 && v[1] < 15 ? static_cast<uint8_t>(v[1]) : kRandomMap;
    s.hideSecs = static_cast<uint16_t>(std::clamp(v[2], 10, 600));
    s.seekSecs = static_cast<uint16_t>(std::clamp(v[3], 30, 1800));
    s.hunters = static_cast<uint8_t>(std::clamp(v[4], 0, 8));
    s.foundJoinHunters = v[5] & 1;
    s.missPenaltyQuarters = (v[5] & 2) ? 2 : 0;
    s.finalClueSecs = (v[5] & 4) ? 20 : 0;
    s.autoNext = v[5] & 8;
    s.isPublic = v[5] & 16;
    s.trackingPulse = v[5] & 32;
    s.treasure = v[5] & 64;
    if (fields >= 7) s.idleTauntSecs = static_cast<uint16_t>(std::clamp(v[6], 0, 600));
    if (fields >= 8) s.freeDecoys = static_cast<uint8_t>(std::clamp(v[7], 0, 10));
    if (fields >= 9) s.missPenaltyQuarters = static_cast<uint8_t>(std::clamp(v[8], 0, 4));
    if (fields >= 10) s.finalClueSecs = static_cast<uint16_t>(std::clamp(v[9], 0, 60));
    return s;
}

inline std::string format_legacy_rules(const match::Settings& s) {
    const int flags = (s.foundJoinHunters ? 1 : 0) | (s.missPenaltyQuarters ? 2 : 0) | (s.finalClueSecs ? 4 : 0) |
                      (s.autoNext ? 8 : 0) | (s.isPublic ? 16 : 0) | (s.trackingPulse ? 32 : 0) |
                      (s.treasure ? 64 : 0);
    char text[80];
    std::snprintf(text, sizeof(text), "0,%d,%u,%u,%u,%d,%u,%u,%u,%u", s.map, s.hideSecs, s.seekSecs,
        s.hunters, flags, s.idleTauntSecs, s.freeDecoys, s.missPenaltyQuarters, s.finalClueSecs);
    return text;
}

// Only the old stock pacing/clue profile is replaced. Explicit times, disabled clues, decoy
// allowances, chosen maps and other custom rules survive. Tracking did not exist in old saves.
inline std::string upgrade_rules(const std::string& text) {
    if (text.empty()) return text;
    auto s = parse_legacy_rules(text);
    int commas = static_cast<int>(std::count(text.begin(), text.end(), ','));
    if (commas < 5) return text;
    if (s.hideSecs == 45 && s.seekSecs == 240) {
        s.hideSecs = 30;
        s.seekSecs = 180;
        if (!s.finalClueSecs && (commas < 6 || s.idleTauntSecs == 60)) {
            s.finalClueSecs = 20;
            s.idleTauntSecs = 20;
        }
    }
    s.trackingPulse = true;
    return format_legacy_rules(s);
}

inline std::string upgrade_treasure_rules(const std::string& text) {
    if (text.empty()) return text;
    auto s = parse_legacy_rules(text);
    s.treasure = true;
    // Only the previous recommended allowance changes; customized allowances are preserved.
    if (s.freeDecoys == 5 && s.hideSecs == 30 && s.seekSecs == 180 &&
        s.idleTauntSecs == 20 && s.finalClueSecs && s.trackingPulse) s.freeDecoys = 3;
    return format_legacy_rules(s);
}

inline std::string upgrade_balance_rules(const std::string& text) {
    if (text.empty()) return text;
    auto s = parse_legacy_rules(text);
    if (s.hideSecs == 30 && s.seekSecs == 180 && s.hunters == 0 && s.idleTauntSecs == 20 &&
        s.finalClueSecs && s.trackingPulse && s.freeDecoys == 3) s.idleTauntSecs = 30;
    return format_legacy_rules(s);
}

inline std::string upgrade_search_rules(const std::string& text, bool legacy = false) {
    if (text.empty()) return text;
    // New-format saves already express an explicit choice. Legacy automatic clues become a
    // single finale; legacy Off stays Off. Remove only the previous stock stationary profile.
    if (!legacy && std::count(text.begin(), text.end(), ',') >= 9) return text;
    auto s = parse_legacy_rules(text);
    if (s.hideSecs == 30 && s.seekSecs == 180 && s.hunters == 0 && s.idleTauntSecs == 30 &&
        s.finalClueSecs && s.trackingPulse && s.freeDecoys == 3) s.idleTauntSecs = 0;
    return format_legacy_rules(s);
}

// v0.4: Prop Hunt only. Drop the mode, reset the map to Random (the map list changed) and turn on
// the new decoy swap and hunter whistle, keeping every other custom choice.
inline std::string upgrade_prop_hunt_rules(const std::string& text) {
    if (text.empty()) return text;
    auto s = parse_legacy_rules(text);
    s.map = kRandomMap;
    s.decoySwap = true;
    s.whistle = true;
    return format_rules(s);
}
}  // namespace hs::settings
