#include "ui.hpp"
#include "ui_style.hpp"

#include "common.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "settings.hpp"

#include <mods/svc/ui.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace hs::ui {

namespace {

UiMenuTabHandle s_menuTab = 0;
UiWindowHandle s_window = 0;

// Play tab elements, re-acquired on every build.
struct PlayTab {
    UiElementHandle status = 0;
    UiElementHandle hostGroup = 0;
    UiElementHandle joinGroup = 0;
    UiElementHandle roomGroup = 0;
    UiElementHandle code = 0;
    UiElementHandle roomRole = 0;
    UiElementHandle hostControls = 0;
    UiElementHandle startRound = 0;
    UiElementHandle endRound = 0;
    UiElementHandle joinWaiting = 0;
    UiListHandle players = 0;
    UiListHandle rooms = 0;
    UiElementHandle roomsStatus = 0;
    std::string lastPlayers;
    int onlineView = -1;
} s_play;

std::vector<net::PublicRoom> s_rooms;
bool s_roomsLoading = false;
std::string s_error;

const char* const kHunters[] = {"Auto (map-aware)", "1", "2", "3", "4", "5", "6", "7", "8"};
const char* const kIdleTaunts[] = {
    "Off", "15 seconds", "20 seconds", "30 seconds", "45 seconds", "60 seconds", "90 seconds", "120 seconds"};
constexpr uint16_t kIdleTauntValues[] = {0, 15, 20, 30, 45, 60, 90, 120};
const char* const kMissPenalties[] = {"Off", "Quarter heart", "Half heart (recommended)", "Three-quarter heart", "One heart"};

std::vector<const char*>& map_options() {
    static std::vector<const char*> options;
    if (options.empty()) {
        options.push_back("Random every round");
        for (int i = 0; i < map_count(); ++i) options.push_back(map_info(i).name);
    }
    return options;
}

std::vector<const char*>& color_options() {
    static std::vector<const char*> options;
    if (options.empty()) {
        for (const TunicColor& c : kColors) options.push_back(c.name);
    }
    return options;
}

void toast(const std::string& title, const std::string& body, const char* type = nullptr) {
    UiToastDesc t = UI_TOAST_DESC_INIT;
    t.type = type;
    t.title_rml = title.c_str();
    t.body_rml = body.c_str();
    svc_ui->push_toast(mod_ctx, &t);
}

// ---- rules -----------------------------------------------------------------------------------

enum Field : intptr_t {
    F_MAP,
    F_HIDE,
    F_SEEK,
    F_HUNTERS,
    F_DECOYS,
    F_JOIN,
    F_PENALTY,
    F_TAUNT,
    F_TRACKING,
    F_TREASURE,
    F_SWAP,
    F_IDLE_TAUNT,
    F_NEXT,
    F_PUBLIC
};

int idle_taunt_option(uint16_t seconds) {
    for (size_t i = 0; i < std::size(kIdleTauntValues); ++i) {
        if (kIdleTauntValues[i] == seconds) return static_cast<int>(i);
    }
    return 0;
}

match::Settings current_rules() {
    return net::status() == net::Status::Online ? match::get().settings : settings::host_rules();
}

void get_rule(ModContext*, void* user, UiControlValue* out) {
    const match::Settings s = current_rules();
    switch (static_cast<Field>(reinterpret_cast<intptr_t>(user))) {
    case F_MAP: out->int_value = s.map == kRandomMap ? 0 : s.map + 1; break;
    case F_HIDE: out->int_value = s.hideSecs; break;
    case F_SEEK: out->int_value = s.seekSecs; break;
    case F_HUNTERS: out->int_value = s.hunters; break;
    case F_DECOYS: out->int_value = s.freeDecoys; break;
    case F_JOIN: out->bool_value = s.foundJoinHunters; break;
    case F_PENALTY: out->int_value = s.missPenaltyQuarters; break;
    case F_TAUNT: out->int_value = s.finalClueSecs; break;
    case F_TRACKING: out->bool_value = s.trackingPulse; break;
    case F_TREASURE: out->bool_value = s.treasure; break;
    case F_SWAP: out->bool_value = s.decoySwap; break;
    case F_IDLE_TAUNT: out->int_value = idle_taunt_option(s.idleTauntSecs); break;
    case F_NEXT: out->bool_value = s.autoNext; break;
    case F_PUBLIC: out->bool_value = s.isPublic; break;
    }
}

void set_rule(ModContext*, void* user, const UiControlValue* v) {
    match::Settings s = current_rules();
    switch (static_cast<Field>(reinterpret_cast<intptr_t>(user))) {
    case F_MAP: s.map = v->int_value <= 0 ? kRandomMap : static_cast<uint8_t>(v->int_value - 1); break;
    case F_HIDE: s.hideSecs = static_cast<uint16_t>(v->int_value); break;
    case F_SEEK: s.seekSecs = static_cast<uint16_t>(v->int_value); break;
    case F_HUNTERS: s.hunters = static_cast<uint8_t>(v->int_value); break;
    case F_DECOYS: s.freeDecoys = static_cast<uint8_t>(v->int_value); break;
    case F_JOIN: s.foundJoinHunters = v->bool_value; break;
    case F_PENALTY: s.missPenaltyQuarters = static_cast<uint8_t>(std::clamp<int64_t>(v->int_value, 0, 4)); break;
    case F_TAUNT: s.finalClueSecs = static_cast<uint16_t>(std::clamp<int64_t>(v->int_value, 0, 60)); break;
    case F_TRACKING: s.trackingPulse = v->bool_value; break;
    case F_TREASURE: s.treasure = v->bool_value; break;
    case F_SWAP: s.decoySwap = v->bool_value; break;
    case F_IDLE_TAUNT: {
        const int option = std::clamp<int>(static_cast<int>(v->int_value), 0,
            static_cast<int>(std::size(kIdleTauntValues)) - 1);
        s.idleTauntSecs = kIdleTauntValues[option];
        break;
    }
    case F_NEXT: s.autoNext = v->bool_value; break;
    case F_PUBLIC: s.isPublic = v->bool_value; break;
    }
    settings::save_host_rules(s);
    if (net::is_host()) match::set_settings(s);
}

bool rules_locked(ModContext*, void*) {
    return match::in_round() || (net::status() == net::Status::Online && !net::is_host());
}

void add_rule(UiElementHandle pane, UiControlKind kind, const char* label, const char* help, Field f,
    const char* const* options = nullptr, size_t optionCount = 0, int64_t min = 0, int64_t max = 0,
    int64_t step = 1, const char* suffix = nullptr) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.kind = kind;
    c.label = label;
    c.help_rml = help;
    c.get = get_rule;
    c.set = set_rule;
    c.is_disabled = rules_locked;
    c.user_data = reinterpret_cast<void*>(static_cast<intptr_t>(f));
    c.options = options;
    c.option_count = optionCount;
    c.min = min;
    c.max = max;
    c.step = step;
    c.suffix = suffix;
    svc_ui->pane_add_control(mod_ctx, pane, &c, nullptr);
}

ModResult build_rules(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    svc_ui->elem_set_class(mod_ctx, left, "hs-main", true);
    svc_ui->elem_set_class(mod_ctx, right, "hs-help", true);
    svc_ui->pane_add_section(mod_ctx, left, "Round");
    add_rule(left, UI_CONTROL_DROPDOWN, "Map", "Random picks a new arena every round and skips the large ones with fewer than six players. Any arena can be chosen explicitly.",
        F_MAP, map_options().data(), map_options().size());
    add_rule(left, UI_CONTROL_NUMBER, "Hiding time", "How long hunters wait with a black screen.", F_HIDE,
        nullptr, 0, 15, 180, 5, " s");
    add_rule(left, UI_CONTROL_NUMBER, "Round time", "How long hunters have to find everyone.", F_SEEK,
        nullptr, 0, 60, 900, 30, " s");
    add_rule(left, UI_CONTROL_DROPDOWN, "Hunters", "Auto: one hunter per four players on compact maps, per three on large maps, rounded up. At least one hider remains.", F_HUNTERS, kHunters, std::size(kHunters));
    svc_ui->pane_add_section(mod_ctx, left, "Hunt balance");
    add_rule(left, UI_CONTROL_NUMBER, "Free decoys",
        "A disguised hider can place these during hiding or hunting with D-pad up. After using them, "
        "extra decoys cost 3 points earned in the current round and can only be bought during the hunt.",
        F_DECOYS, nullptr, 0, 0, 10, 1);
    add_rule(left, UI_CONTROL_TOGGLE, "Decoy swap",
        "Props hold D-pad up to trade places with their newest decoy: they appear where it stood and "
        "it takes their old spot. Two swaps a round, 15 seconds apart.", F_SWAP);
    add_rule(left, UI_CONTROL_TOGGLE, "Found hiders become hunters",
        "On: a found prop becomes a hunter. Off: they watch until the next round.", F_JOIN);
    add_rule(left, UI_CONTROL_DROPDOWN, "Miss penalty",
        "Half a heart by default, including hits on decoys. Zero hearts eliminates the hunter; "
        "hiders win when all hunters are out. Only a confirmed find restores one heart.",
        F_PENALTY, kMissPenalties, std::size(kMissPenalties));
    add_rule(left, UI_CONTROL_DROPDOWN, "Stationary clue",
        "A hider that has not moved this long automatically taunts. Moving resets the timer. "
        "Off by default, so a convincing hiding spot is safe.",
        F_IDLE_TAUNT, kIdleTaunts, std::size(kIdleTaunts));
    add_rule(left, UI_CONTROL_NUMBER, "Final clue",
        "One exact location marker per remaining hider for three seconds, this many seconds before the hunt ends. "
        "20 by default; 0 disables it for voluntary clues only. In short rounds it occurs no earlier "
        "than halfway. The marker fires on time even after a recent taunt or pickup.", F_TAUNT,
        nullptr, 0, 0, 60, 5, "seconds remaining (0 = Off)");
    add_rule(left, UI_CONTROL_TOGGLE, "Hunter tracking",
        "D-pad down gives a three-second direction and rough range to the nearest hider. "
        "25-second cooldown; no name or exact world marker.", F_TRACKING);
    add_rule(left, UI_CONTROL_TOGGLE, "Treasure rupees",
        "Up to 24 rupees spread across reachable ground. Pickups give 1 point (2 in Treasure Rush); "
        "the third adds up to 2. Loot, taunts and style awards share an 8-point round bonus limit. Each pickup "
        "gives hunters a rough direction and range for three seconds; no exact marker.", F_TREASURE);
    svc_ui->pane_add_section(mod_ctx, left, "Lobby");
    add_rule(left, UI_CONTROL_TOGGLE, "Automatic rounds",
        "After the scoreboard, a new round starts with new hunters.", F_NEXT);
    add_rule(left, UI_CONTROL_TOGGLE, "Public lobby",
        "Anyone can find and join it from the public list.", F_PUBLIC);

    UiControlDesc reset = UI_CONTROL_DESC_INIT;
    reset.kind = UI_CONTROL_BUTTON;
    reset.label = "Reset scores";
    reset.on_pressed = [](ModContext*, void*) { match::reset_scores(); };
    reset.is_disabled = [](ModContext*, void*) { return !net::is_host(); };
    svc_ui->pane_add_control(mod_ctx, left, &reset, nullptr);
    UiControlDesc recommended = UI_CONTROL_DESC_INIT;
    recommended.kind = UI_CONTROL_BUTTON;
    recommended.label = "Use recommended rules";
    recommended.is_disabled = rules_locked;
    recommended.on_pressed = [](ModContext*, void*) {
        const auto current = current_rules();
        match::Settings balanced;
        balanced.map = current.map;
        balanced.isPublic = current.isPublic;
        settings::save_host_rules(balanced);
        if (net::is_host()) match::set_settings(balanced);
    };
    svc_ui->pane_add_control(mod_ctx, left, &recommended, nullptr);
    return MOD_OK;
}

// ---- play ------------------------------------------------------------------------------------

void join_code(const std::string& code) {
    s_error.clear();
    settings::set_room_code(code);
    net::join_room(settings::server(), code, settings::name());
}

void refresh_rooms() {
    if (s_roomsLoading) return;
    s_roomsLoading = true;
    net::fetch_rooms(settings::server(), [](bool ok, std::vector<net::PublicRoom> rooms) {
        s_roomsLoading = false;
        s_rooms = std::move(rooms);
        if (s_play.rooms != 0) {
            std::vector<std::string> labels;
            std::vector<UiListItem> items;
            labels.reserve(s_rooms.size());
            for (const net::PublicRoom& r : s_rooms) {
                const std::string map = r.map >= 0 && r.map < map_count() ? map_info(r.map).name : "Random";
                labels.push_back((r.label.empty() ? r.code : r.label) + "  |  " +
                    std::to_string(r.players) + "/" + std::to_string(r.max) + "  |  " + map);
            }
            for (size_t i = 0; i < s_rooms.size(); ++i) {
                UiListItem item = UI_LIST_ITEM_INIT;
                item.key = i;
                item.label = labels[i].c_str();
                items.push_back(item);
            }
            svc_ui->list_set_items(mod_ctx, s_play.rooms, items.data(), items.size());
        }
        if (s_play.roomsStatus != 0) {
            const char* text = !ok ? "Couldn't reach the server." :
                               s_rooms.empty() ? "No public games right now. Host one!" : "Press a game to join it.";
            svc_ui->elem_set_text(mod_ctx, s_play.roomsStatus, text);
        }
    });
}

UiElementHandle button(UiElementHandle pane, const char* label, UiPressedFn pressed,
    UiPredicateFn disabled = nullptr, const char* help = nullptr) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.kind = UI_CONTROL_BUTTON;
    c.label = label;
    c.help_rml = help;
    c.on_pressed = pressed;
    c.is_disabled = disabled;
    UiElementHandle h = 0;
    svc_ui->pane_add_control(mod_ctx, pane, &c, &h);
    return h;
}

std::string role_name(Role r) {
    switch (r) {
    case Role::Hider: return "Prop";
    case Role::Hunter: return "Hunter";
    case Role::Spectator: return "Watching";
    default: return "In lobby";
    }
}

void update_players() {
    if (s_play.players == 0) return;
    std::vector<std::string> labels;
    std::vector<int> ids;
    for (int id = 1; id <= kMaxPlayers; ++id) {
        const match::Player& p = match::player(id);
        if (!net::member(id).present) continue;
        std::string label = std::string(net::member(id).name);
        if (id == net::self_id()) label += " (you)";
        if (id == net::host_id()) label += " (host)";
        label += "  |  " + role_name(p.role) + "  |  " + std::to_string(p.score) + " pts";
        labels.push_back(label);
        ids.push_back(id);
    }
    std::string joined;
    for (const std::string& l : labels) joined += l + "\n";
    if (joined == s_play.lastPlayers) return;
    s_play.lastPlayers = joined;
    std::vector<UiListItem> items;
    for (size_t i = 0; i < labels.size(); ++i) {
        UiListItem item = UI_LIST_ITEM_INIT;
        item.key = static_cast<uint64_t>(ids[i]);
        item.label = labels[i].c_str();
        items.push_back(item);
    }
    svc_ui->list_set_items(mod_ctx, s_play.players, items.data(), items.size());
}

void reset_play_detail() {
    s_play.code = 0;
    s_play.roomRole = 0;
    s_play.hostControls = 0;
    s_play.startRound = 0;
    s_play.endRound = 0;
    s_play.joinWaiting = 0;
    s_play.players = 0;
    s_play.rooms = 0;
    s_play.roomsStatus = 0;
    s_play.lastPlayers.clear();
}

void add_identity(UiElementHandle pane) {
    svc_ui->pane_add_section(mod_ctx, pane, "Your player");
    UiControlDesc name = UI_CONTROL_DESC_INIT;
    name.kind = UI_CONTROL_STRING;
    name.label = "Your name";
    name.binding = UI_BINDING_CONFIG_VAR;
    name.config_var = settings::name_var();
    name.max_length = 20;
    svc_ui->pane_add_control(mod_ctx, pane, &name, nullptr);

    UiControlDesc color = UI_CONTROL_DESC_INIT;
    color.kind = UI_CONTROL_DROPDOWN;
    color.label = "Tunic colour";
    color.help_rml = "Everyone in a room gets a different colour; you get this one if it's free.";
    color.options = color_options().data();
    color.option_count = color_options().size();
    color.get = [](ModContext*, void*, UiControlValue* out) { out->int_value = settings::color(); };
    color.set = [](ModContext*, void*, const UiControlValue* v) {
        settings::set_color(static_cast<uint8_t>(v->int_value));
        match::set_wanted_color(static_cast<uint8_t>(v->int_value));
    };
    svc_ui->pane_add_control(mod_ctx, pane, &color, nullptr);
}

ModResult build_host_page(ModContext*, UiElementHandle pane, void*, ModError*) {
    reset_play_detail();
    svc_ui->pane_add_section(mod_ctx, pane, "Host a game");
    svc_ui->pane_add_rml(mod_ctx, pane,
        "<div class='hs-guide'><h3>Bring your friends</h3><p>Create a lobby and share its code. "
        "Choose the map in Rules, then start a round.</p></div>", nullptr);
    add_identity(pane);

    const auto offline = [](ModContext*, void*) { return net::status() != net::Status::Offline; };
    const auto create = button(pane, "Create lobby", [](ModContext*, void*) {
        s_error.clear();
        net::host_room(settings::server(), settings::name());
    }, offline, "Creates a room and copies its code to your clipboard. Send it to your friends.");
    svc_ui->elem_set_class(mod_ctx, create, "hs-primary", true);
    return MOD_OK;
}

ModResult build_join_page(ModContext*, UiElementHandle pane, void*, ModError*) {
    reset_play_detail();
    svc_ui->pane_add_section(mod_ctx, pane, "Join a game");
    svc_ui->pane_add_rml(mod_ctx, pane,
        "<div class='hs-guide'><h3>Find your friends</h3><p>Enter a lobby code or pick a public game.</p></div>",
        nullptr);
    add_identity(pane);
    const auto offline = [](ModContext*, void*) { return net::status() != net::Status::Offline; };
    UiControlDesc code = UI_CONTROL_DESC_INIT;
    code.kind = UI_CONTROL_STRING;
    code.label = "Room code";
    code.binding = UI_BINDING_CONFIG_VAR;
    code.config_var = settings::room_code_var();
    code.max_length = 8;
    svc_ui->pane_add_control(mod_ctx, pane, &code, nullptr);
    const auto join = button(pane, "Join lobby",
        [](ModContext*, void*) { join_code(settings::room_code()); }, offline);
    svc_ui->elem_set_class(mod_ctx, join, "hs-primary", true);
    button(pane, "Paste code and join", [](ModContext*, void*) {
        char buf[64] = {};
        if (svc_ui->get_clipboard_text(mod_ctx, buf, sizeof(buf), nullptr) == MOD_OK) join_code(buf);
    }, offline);

    svc_ui->pane_add_section(mod_ctx, pane, "Public games");
    svc_ui->pane_add_text(mod_ctx, pane, "Loading...", &s_play.roomsStatus);
    button(pane, "Refresh list", [](ModContext*, void*) { refresh_rooms(); }, offline);
    UiListDesc rooms = UI_LIST_DESC_INIT;
    rooms.on_pressed = [](ModContext*, UiListHandle, uint64_t key, void*) {
        if (key < s_rooms.size() && net::status() == net::Status::Offline)
            join_code(s_rooms[key].code);
    };
    svc_ui->pane_add_list(mod_ctx, pane, &rooms, &s_play.rooms);
    refresh_rooms();
    return MOD_OK;
}

ModResult build_room_page(ModContext*, UiElementHandle pane, void*, ModError*) {
    reset_play_detail();
    svc_ui->pane_add_section(mod_ctx, pane, "Current room");
    svc_ui->pane_add_text(mod_ctx, pane, "", &s_play.roomRole);
    svc_ui->pane_add_text(mod_ctx, pane, "", &s_play.code);
    svc_ui->elem_set_class(mod_ctx, s_play.roomRole, "hs-muted", true);
    svc_ui->elem_set_class(mod_ctx, s_play.code, "hs-code", true);
    const auto notOnline = [](ModContext*, void*) { return net::status() != net::Status::Online; };
    button(pane, "Copy room code", [](ModContext*, void*) {
        svc_ui->set_clipboard_text(mod_ctx, net::room_code().c_str());
        toast("Hide & Seek", "Room code copied.");
    }, notOnline);

    svc_ui->pane_add_rml(mod_ctx, pane,
        "<div class='hs-guide'><p>Choose a map in Rules. Start when everyone is ready.</p></div>", &s_play.hostControls);
    s_play.startRound = button(pane, "Start round", [](ModContext*, void*) {
        std::string why;
        if (!match::can_start(&why)) {
            toast("Hide & Seek", why, "warning");
            return;
        }
        match::start_round();
        if (s_window != 0) svc_ui->window_close(mod_ctx, s_window);
    }, [](ModContext*, void*) { return !net::is_host() || match::in_round(); },
        "Everyone is warped to the map. Hunters wait while the props hide.");
    svc_ui->elem_set_class(mod_ctx, s_play.startRound, "hs-primary", true);
    s_play.endRound = button(pane, "End round now", [](ModContext*, void*) { match::end_round(); },
        [](ModContext*, void*) { return !net::is_host() || !match::in_round(); });
    svc_ui->pane_add_rml(mod_ctx, pane,
        "<p><b>Waiting for the host.</b> You can see the chosen rules in the Rules tab.</p>",
        &s_play.joinWaiting);

    svc_ui->pane_add_section(mod_ctx, pane, "Players");
    UiListDesc players = UI_LIST_DESC_INIT;
    players.on_pressed = [](ModContext*, UiListHandle, uint64_t, void*) {};
    svc_ui->pane_add_list(mod_ctx, pane, &players, &s_play.players);
    button(pane, "Leave room", [](ModContext*, void*) {
        net::leave_room();
        match::on_disconnected();
    }, [](ModContext*, void*) { return net::status() == net::Status::Offline; });
    return MOD_OK;
}

ModResult build_play(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_play = PlayTab{};
    svc_ui->elem_set_class(mod_ctx, left, "hs-nav", true);
    svc_ui->elem_set_class(mod_ctx, right, "hs-detail", true);
    svc_ui->pane_add_rml(mod_ctx, left,
        "<div class='hs-brand'><small>HYRULE</small><h1>Hide &amp; Seek</h1></div>", nullptr);
    svc_ui->pane_add_text(mod_ctx, left, "", &s_play.status);
    svc_ui->elem_set_class(mod_ctx, s_play.status, "hs-status", true);
    svc_ui->pane_add_section(mod_ctx, left, "Play");

    UiGroupDesc host = UI_GROUP_DESC_INIT;
    host.label = "Host";
    host.build = build_host_page;
    svc_ui->pane_add_group(mod_ctx, left, right, &host, &s_play.hostGroup);

    UiGroupDesc join = UI_GROUP_DESC_INIT;
    join.label = "Join";
    join.build = build_join_page;
    svc_ui->pane_add_group(mod_ctx, left, right, &join, &s_play.joinGroup);

    UiGroupDesc room = UI_GROUP_DESC_INIT;
    room.label = "Your lobby";
    room.build = build_room_page;
    svc_ui->pane_add_group(mod_ctx, left, right, &room, &s_play.roomGroup);
    return MOD_OK;
}

ModResult update_play(ModContext*, void*, ModError*) {
    const bool online = net::status() == net::Status::Online;
    const int wantedView = online ? 1 : 0;
    if (wantedView != s_play.onlineView) {
        if (s_play.hostGroup != 0) svc_ui->elem_set_visible(mod_ctx, s_play.hostGroup, !online);
        if (s_play.joinGroup != 0) svc_ui->elem_set_visible(mod_ctx, s_play.joinGroup, !online);
        if (s_play.roomGroup != 0) svc_ui->elem_set_visible(mod_ctx, s_play.roomGroup, online);
        s_play.onlineView = wantedView;
        const UiElementHandle focus = online ? s_play.roomGroup : s_play.hostGroup;
        if (focus != 0) svc_ui->elem_focus(mod_ctx, focus);
    }

    std::string status;
    switch (net::status()) {
    case net::Status::Offline:
        status = s_error.empty() ? "2-16 players. Hide, hunt, repeat." : s_error;
        break;
    case net::Status::Connecting: status = "Connecting..."; break;
    case net::Status::Online:
        status = "Room " + net::room_code() + "  |  " + std::to_string(net::member_count()) + "/16 players";
        break;
    }
    if (s_play.status != 0) svc_ui->elem_set_text(mod_ctx, s_play.status, status.c_str());
    if (s_play.code != 0) {
        const std::string code = net::status() == net::Status::Online
                                     ? net::room_code()
                                     : "Not in a room";
        svc_ui->elem_set_text(mod_ctx, s_play.code, code.c_str());
    }
    if (s_play.roomRole != 0) {
        const char* role = net::is_host() ? "You're hosting" : "You're in. Waiting for the host.";
        svc_ui->elem_set_text(mod_ctx, s_play.roomRole, role);
    }
    const bool roomHost = online && net::is_host();
    if (s_play.hostControls != 0) {
        svc_ui->elem_set_visible(mod_ctx, s_play.hostControls, roomHost);
    }
    if (s_play.startRound != 0) svc_ui->elem_set_visible(mod_ctx, s_play.startRound, roomHost && !match::in_round());
    if (s_play.endRound != 0) svc_ui->elem_set_visible(mod_ctx, s_play.endRound, roomHost && match::in_round());
    if (s_play.joinWaiting != 0) {
        svc_ui->elem_set_visible(mod_ctx, s_play.joinWaiting, online && !roomHost);
    }
    update_players();
    return MOD_OK;
}

// ---- help and settings -----------------------------------------------------------------------

ModResult build_help(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    svc_ui->elem_set_class(mod_ctx, left, "hs-full", true);
    svc_ui->elem_set_class(mod_ctx, right, "hs-unused", true);
    svc_ui->pane_add_rml(mod_ctx, left,
        "<div class='hs-guide'><h3>Hide</h3><p>Become something that belongs on the map. "
        "Stand next to a real pot, crate, pumpkin, sign, cat, chair... and press D-pad right to "
        "copy it, facing the same way. With nothing nearby, D-pad right and left cycle through "
        "the map's objects. Nothing in the world answers the A button during a round, so a "
        "disguise looks and behaves exactly like the real thing.</p></div>", nullptr);
    svc_ui->pane_add_rml(mod_ctx, left,
        "<div class='hs-guide'><h3>Decoys and swaps</h3><p>Tap D-pad up to leave a copy of your "
        "disguise behind; you start with three, extras cost 3 round points. During the hunt, hold "
        "D-pad up to swap places with your newest decoy: you appear where it stood and it takes "
        "your spot (two swaps a round, 15 seconds apart). A hunter striking your decoy earns you a "
        "point, up to three times per round.</p></div>", nullptr);
    svc_ui->pane_add_rml(mod_ctx, left,
        "<div class='hs-guide'><h3>Take a risk</h3><p>D-pad down taunts: 1 point, or 2 with a "
        "hunter close by. A hunter's sword missing right next to you is a close call, also worth a "
        "point. Rupees give points too. Taunts and rupees show hunters a direction arrow for "
        "3 seconds; near the end everyone left is revealed once. Style bonuses are capped at "
        "8 points a round.</p></div>", nullptr);
    svc_ui->pane_add_rml(mod_ctx, left,
        "<div class='hs-guide'><h3>Hunt</h3><p>B swings your sword; while swimming it tags a "
        "nearby prop. Misses cost half a heart by default and decoys count as misses. At zero "
        "hearts you watch until the next round; only finding a prop restores a heart. D-pad down "
        "tracks the nearest prop (25 s). The first find of the round and quick finds earn extra "
        "points.</p></div>", nullptr);
    svc_ui->pane_add_rml(mod_ctx, left,
        "<div class='hs-guide'><h3>Play again</h3><p>Props earn up to 12 points across the hunt "
        "and 6 for surviving; the last prop standing gets 2 more. Starting hunters share up to 12 "
        "capture points and get 6 for finding everyone. Results crown the MVP, best disguise, "
        "sharpshooter and trickster. Hunters rotate each round. Start from the Prop Hunt "
        "game mode for matching worlds. On mobile, use Dusklight's touch D-pad and B "
        "button.</p></div>", nullptr);
    return MOD_OK;
}

ModResult build_settings(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    svc_ui->elem_set_class(mod_ctx, left, "hs-main", true);
    svc_ui->elem_set_class(mod_ctx, right, "hs-help", true);
    svc_ui->pane_add_section(mod_ctx, left, "Display");
    UiControlDesc tags = UI_CONTROL_DESC_INIT;
    tags.kind = UI_CONTROL_TOGGLE;
    tags.label = "Show name tags";
    tags.binding = UI_BINDING_CONFIG_VAR;
    tags.config_var = settings::name_tags_var();
    svc_ui->pane_add_control(mod_ctx, left, &tags, nullptr);

    UiControlDesc hints = UI_CONTROL_DESC_INIT;
    hints.kind = UI_CONTROL_TOGGLE;
    hints.label = "Control hints";
    hints.help_rml = "On by default: show a small reminder above your status card.";
    hints.binding = UI_BINDING_CONFIG_VAR;
    hints.config_var = settings::control_hints_var();
    svc_ui->pane_add_control(mod_ctx, left, &hints, nullptr);
    svc_ui->pane_add_section(mod_ctx, left, "Connection");
    UiControlDesc server = UI_CONTROL_DESC_INIT;
    server.kind = UI_CONTROL_STRING;
    server.label = "Server address";
    server.help_rml = "The default public relay is ready to use. Change this only to join a private relay. "
                      "Everyone in a room must use the same server.";
    server.binding = UI_BINDING_CONFIG_VAR;
    server.config_var = settings::server_var();
    svc_ui->pane_add_control(mod_ctx, left, &server, nullptr);
    button(left, "Use a server on this computer", [](ModContext*, void*) {
        svc_config->set_string(mod_ctx, settings::server_var(), "ws://127.0.0.1:8787");
    }, nullptr, "For testing: run <b>node node-server.mjs</b> in the mod's server folder.");
    button(left, "Use the default server", [](ModContext*, void*) {
        svc_config->set_string(mod_ctx, settings::server_var(), HS_DEFAULT_SERVER);
    });
    return MOD_OK;
}

void push_window() {
    if (s_window != 0) return;
    static UiTabDesc tabs[4] = {UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT};
    tabs[0].title = "Play";
    tabs[0].build = build_play;
    tabs[0].update = update_play;
    tabs[1].title = "Rules";
    tabs[1].build = build_rules;
    tabs[2].title = "Guide";
    tabs[2].build = build_help;
    tabs[3].title = "Settings";
    tabs[3].build = build_settings;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 4;
    desc.rcss = kWindowStyle;
    desc.on_closed = [](ModContext*, UiWindowHandle, void*) {
        s_window = 0;
        s_play = PlayTab{};
    };
    svc_ui->window_push(mod_ctx, &desc, &s_window);
}

void open_window(ModContext*, void*) {
    push_window();
}

ModResult build_mods_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    svc_ui->pane_add_text(mod_ctx, panel,
        "Online Prop Hunt for 2-16 players. Open it from the Hide & Seek tab in the menu bar.",
        nullptr);
    button(panel, "Open Hide & Seek", open_window);
    return MOD_OK;
}

}  // namespace

bool init() {
    UiMenuTabDesc tab = UI_MENU_TAB_DESC_INIT;
    tab.label = "Hide & Seek";
    tab.on_selected = open_window;
    if (svc_ui->register_menu_tab(mod_ctx, &tab, &s_menuTab) != MOD_OK) {
        mods::log::warn("could not add the Hide & Seek menu tab");
    }
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_mods_panel;
    svc_ui->register_mods_panel(mod_ctx, &panel);
    return true;
}

void open() {
    push_window();
}

bool is_open() { return s_window != 0; }

void shutdown() {
    s_window = 0;
    s_play = PlayTab{};
}

void on_welcome() {
    if (net::is_host()) {
        svc_ui->set_clipboard_text(mod_ctx, net::room_code().c_str());
        toast("Room " + net::room_code(), "The code is on your clipboard. Send it to your friends!");
    } else {
        toast("Hide & Seek", "Joined room " + net::room_code() + ".");
    }
}

void on_disconnected(const std::string& why) {
    s_error = why;
    toast("Hide & Seek", why, "warning");
}

}  // namespace hs::ui
