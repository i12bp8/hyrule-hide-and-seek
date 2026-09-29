#include "settings.hpp"

#include "maps.hpp"
#include "rules_config.hpp"

#include <algorithm>
#include <cstdio>
#include <random>

namespace hs::settings {

namespace {

ConfigVarHandle s_name = 0;
ConfigVarHandle s_color = 0;
ConfigVarHandle s_server = 0;
ConfigVarHandle s_room = 0;
ConfigVarHandle s_tags = 0;
ConfigVarHandle s_rules = 0;
ConfigVarHandle s_rulesVersion = 0;

ConfigVarHandle reg(const char* name, ConfigVarType type, int64_t i = 0, bool b = false,
    const char* s = nullptr) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = type;
    desc.default_int = i;
    desc.default_bool = b;
    desc.default_string = s;
    ConfigVarHandle h = 0;
    if (svc_config->register_var(mod_ctx, &desc, &h) != MOD_OK) {
        mods::log::warn("could not register setting {}", name);
        return 0;
    }
    return h;
}

std::string get_str(ConfigVarHandle h) {
    if (h == 0) return {};
    size_t len = 0;
    if (svc_config->get_string(mod_ctx, h, nullptr, 0, &len) != MOD_OK) return {};
    std::string out(len + 1, '\0');
    if (svc_config->get_string(mod_ctx, h, out.data(), out.size(), nullptr) != MOD_OK) return {};
    out.resize(len);
    return out;
}

int64_t get_int(ConfigVarHandle h, int64_t fallback) {
    int64_t v = fallback;
    if (h != 0) svc_config->get_int(mod_ctx, h, &v);
    return v;
}

std::mt19937& rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

}  // namespace

void init() {
    s_name = reg("player_name", CONFIG_VAR_STRING);
    s_color = reg("tunic_color", CONFIG_VAR_INT, -1);
    s_server = reg("server", CONFIG_VAR_STRING, 0, false, HS_DEFAULT_SERVER);
    s_room = reg("room_code", CONFIG_VAR_STRING);
    s_tags = reg("name_tags", CONFIG_VAR_BOOL, 0, true);
    s_rules = reg("host_rules", CONFIG_VAR_STRING);
    s_rulesVersion = reg("host_rules_version", CONFIG_VAR_INT);

    if (s_rulesVersion != 0 && get_int(s_rulesVersion, 0) < 2) {
        const std::string text = get_str(s_rules);
        const std::string migrated = upgrade_rules(text);
        if (s_rules != 0 && migrated != text) svc_config->set_string(mod_ctx, s_rules, migrated.c_str());
        svc_config->set_int(mod_ctx, s_rulesVersion, 2);
    }

    // v0.1.0/v0.1.1 shipped before the public relay was provisioned. Upgrade only that exact
    // placeholder, preserving custom and localhost server addresses.
    if (s_server != 0 && get_str(s_server) == "wss://hyrule-hide-and-seek.example.workers.dev") {
        svc_config->set_string(mod_ctx, s_server, HS_DEFAULT_SERVER);
    }

    if (s_name != 0 && get_str(s_name).empty()) {
        const std::string fallback =
            "Hero" + std::to_string(std::uniform_int_distribution<int>(100, 999)(rng()));
        svc_config->set_string(mod_ctx, s_name, fallback.c_str());
    }
    if (s_color != 0 && get_int(s_color, -1) < 0) {
        svc_config->set_int(mod_ctx, s_color, std::uniform_int_distribution<int>(1, kMaxPlayers - 1)(rng()));
    }
}

std::string name() {
    std::string n = get_str(s_name);
    return n.empty() ? "Hero" : n.substr(0, 20);
}

uint8_t color() {
    const int64_t c = get_int(s_color, 0);
    return static_cast<uint8_t>(c >= 0 && c < kMaxPlayers ? c : 0);
}

void set_color(uint8_t color) {
    if (s_color != 0) svc_config->set_int(mod_ctx, s_color, color);
}

std::string server() {
    std::string s = get_str(s_server);
    return s.empty() ? HS_DEFAULT_SERVER : s;
}

std::string room_code() {
    return get_str(s_room);
}

void set_room_code(const std::string& code) {
    if (s_room != 0) svc_config->set_string(mod_ctx, s_room, code.c_str());
}

bool name_tags() {
    bool v = true;
    if (s_tags != 0) svc_config->get_bool(mod_ctx, s_tags, &v);
    return v;
}

// Stored as "mode,map,hide,seek,hunters,flags,idle-taunt-seconds,free-decoys". Older saves keep
// defaults for fields that did not exist yet.
match::Settings host_rules() {
    return parse_rules(get_str(s_rules));
}

void save_host_rules(const match::Settings& s) {
    if (s_rules == 0) return;
    const std::string text = format_rules(s);
    svc_config->set_string(mod_ctx, s_rules, text.c_str());
}

ConfigVarHandle name_var() {
    return s_name;
}
ConfigVarHandle server_var() {
    return s_server;
}
ConfigVarHandle room_code_var() {
    return s_room;
}
ConfigVarHandle name_tags_var() {
    return s_tags;
}

}  // namespace hs::settings
