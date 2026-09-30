#pragma once
#include "maps.hpp"
#include "match.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace hs::settings {
inline match::Settings parse_rules(const std::string& text) {
    match::Settings s;
    int v[8] = {};
    const int fields = std::sscanf(text.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2],
        &v[3], &v[4], &v[5], &v[6], &v[7]);
    if (fields < 6) return s;
    s.mode = v[0] == 1 ? Mode::HideAndSeek : Mode::PropHunt;
    s.map = v[1] >= 0 && v[1] < map_count() ? static_cast<uint8_t>(v[1]) : kRandomMap;
    s.hideSecs = static_cast<uint16_t>(std::clamp(v[2], 10, 600));
    s.seekSecs = static_cast<uint16_t>(std::clamp(v[3], 30, 1800));
    s.hunters = static_cast<uint8_t>(std::clamp(v[4], 0, 8));
    s.foundJoinHunters = v[5] & 1;
    s.missPenalty = v[5] & 2;
    s.autoTaunt = v[5] & 4;
    s.autoNext = v[5] & 8;
    s.isPublic = v[5] & 16;
    s.trackingPulse = v[5] & 32;
    s.treasure = v[5] & 64;
    if (fields >= 7) s.idleTauntSecs = static_cast<uint16_t>(std::clamp(v[6], 0, 600));
    if (fields >= 8) s.freeDecoys = static_cast<uint8_t>(std::clamp(v[7], 0, 10));
    return s;
}

inline std::string format_rules(const match::Settings& s) {
    const int flags = (s.foundJoinHunters ? 1 : 0) | (s.missPenalty ? 2 : 0) | (s.autoTaunt ? 4 : 0) |
                      (s.autoNext ? 8 : 0) | (s.isPublic ? 16 : 0) | (s.trackingPulse ? 32 : 0) |
                      (s.treasure ? 64 : 0);
    char text[80];
    std::snprintf(text, sizeof(text), "%d,%d,%u,%u,%u,%d,%u,%u", static_cast<int>(s.mode), s.map,
        s.hideSecs, s.seekSecs, s.hunters, flags, s.idleTauntSecs, s.freeDecoys);
    return text;
}

// Only the old stock pacing/clue profile is replaced. Explicit times, disabled clues, decoy
// allowances, chosen maps and other custom rules survive. Tracking did not exist in old saves.
inline std::string upgrade_rules(const std::string& text) {
    if (text.empty()) return text;
    auto s = parse_rules(text);
    int commas = static_cast<int>(std::count(text.begin(), text.end(), ','));
    if (commas < 5) return text;
    if (s.hideSecs == 45 && s.seekSecs == 240) {
        s.hideSecs = 30;
        s.seekSecs = 180;
        if (!s.autoTaunt && (commas < 6 || s.idleTauntSecs == 60)) {
            s.autoTaunt = true;
            s.idleTauntSecs = 20;
        }
    }
    s.trackingPulse = true;
    return format_rules(s);
}

inline std::string upgrade_treasure_rules(const std::string& text) {
    if (text.empty()) return text;
    auto s = parse_rules(text);
    s.treasure = true;
    // Only the previous recommended allowance changes; customized allowances are preserved.
    if (s.freeDecoys == 5 && s.hideSecs == 30 && s.seekSecs == 180 &&
        s.idleTauntSecs == 20 && s.autoTaunt && s.trackingPulse) s.freeDecoys = 3;
    return format_rules(s);
}

inline std::string upgrade_balance_rules(const std::string& text) {
    if (text.empty()) return text;
    auto s = parse_rules(text);
    if (s.hideSecs == 30 && s.seekSecs == 180 && s.hunters == 0 && s.idleTauntSecs == 20 &&
        s.autoTaunt && s.trackingPulse && s.freeDecoys == 3) s.idleTauntSecs = 30;
    return format_rules(s);
}
}  // namespace hs::settings
