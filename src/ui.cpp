#include "ui.hpp"

#include "common.hpp"
#include "maps.hpp"
#include "match.hpp"
#include "net.hpp"
#include "settings.hpp"

#include <mods/svc/ui.h>

#include <string>
#include <vector>

namespace hs::ui {

namespace {

UiMenuTabHandle s_menuTab = 0;
UiWindowHandle s_window = 0;

// Play tab elements, re-acquired on every build.
struct PlayTab {
    UiElementHandle status = 0;
    UiElementHandle code = 0;
    UiListHandle players = 0;
    UiListHandle rooms = 0;
    UiElementHandle roomsStatus = 0;
    std::string lastPlayers;
} s_play;

std::vector<net::PublicRoom> s_rooms;
bool s_roomsLoading = false;
std::string s_error;

const char* const kModes[] = {"Prop Hunt", "Hide & Seek"};
const char* const kHunters[] = {"Auto (1 per 4 players)", "1", "2", "3", "4"};

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

enum Field : intptr_t { F_MODE, F_MAP, F_HIDE, F_SEEK, F_HUNTERS, F_JOIN, F_PENALTY, F_TAUNT, F_NEXT, F_PUBLIC };

match::Settings current_rules() {
    return net::status() == net::Status::Online ? match::get().settings : settings::host_rules();
}

void get_rule(ModContext*, void* user, UiControlValue* out) {
    const match::Settings s = current_rules();
    switch (static_cast<Field>(reinterpret_cast<intptr_t>(user))) {
    case F_MODE: out->int_value = static_cast<int>(s.mode); break;
    case F_MAP: out->int_value = s.map == kRandomMap ? 0 : s.map + 1; break;
    case F_HIDE: out->int_value = s.hideSecs; break;
    case F_SEEK: out->int_value = s.seekSecs; break;
    case F_HUNTERS: out->int_value = s.hunters; break;
    case F_JOIN: out->bool_value = s.foundJoinHunters; break;
    case F_PENALTY: out->bool_value = s.missPenalty; break;
    case F_TAUNT: out->bool_value = s.autoTaunt; break;
    case F_NEXT: out->bool_value = s.autoNext; break;
    case F_PUBLIC: out->bool_value = s.isPublic; break;
    }
}

void set_rule(ModContext*, void* user, const UiControlValue* v) {
    match::Settings s = current_rules();
    switch (static_cast<Field>(reinterpret_cast<intptr_t>(user))) {
    case F_MODE: s.mode = v->int_value == 1 ? Mode::HideAndSeek : Mode::PropHunt; break;
    case F_MAP: s.map = v->int_value <= 0 ? kRandomMap : static_cast<uint8_t>(v->int_value - 1); break;
    case F_HIDE: s.hideSecs = static_cast<uint16_t>(v->int_value); break;
    case F_SEEK: s.seekSecs = static_cast<uint16_t>(v->int_value); break;
    case F_HUNTERS: s.hunters = static_cast<uint8_t>(v->int_value); break;
    case F_JOIN: s.foundJoinHunters = v->bool_value; break;
    case F_PENALTY: s.missPenalty = v->bool_value; break;
    case F_TAUNT: s.autoTaunt = v->bool_value; break;
    case F_NEXT: s.autoNext = v->bool_value; break;
    case F_PUBLIC: s.isPublic = v->bool_value; break;
    }
    settings::save_host_rules(s);
    if (net::is_host()) match::set_settings(s);
}

bool rules_locked(ModContext*, void*) {
    return net::status() == net::Status::Online && !net::is_host();
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

ModResult build_rules(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, left, "Game");
    add_rule(left, UI_CONTROL_DROPDOWN, "Mode",
        "<b>Prop Hunt</b>: hiders turn into pots, crates and barrels. Hunters must hit them with a "
        "sword before time runs out.<br/><b>Hide &amp; Seek</b>: everyone stays Link; hunters tag "
        "hiders by touching them.",
        F_MODE, kModes, 2);
    add_rule(left, UI_CONTROL_DROPDOWN, "Map", "Where the round is played. Random picks a new map every round.",
        F_MAP, map_options().data(), map_options().size());
    add_rule(left, UI_CONTROL_NUMBER, "Hiding time", "How long hunters wait with a black screen.", F_HIDE,
        nullptr, 0, 15, 180, 5, " s");
    add_rule(left, UI_CONTROL_NUMBER, "Round time", "How long hunters have to find everyone.", F_SEEK,
        nullptr, 0, 60, 900, 30, " s");
    add_rule(left, UI_CONTROL_DROPDOWN, "Hunters", "How many players start as hunters.", F_HUNTERS, kHunters, 5);
    svc_ui->pane_add_section(mod_ctx, left, "Rules");
    add_rule(left, UI_CONTROL_TOGGLE, "Found players join the hunters",
        "On: a found prop becomes a hunter. Off: they watch until the next round.", F_JOIN);
    add_rule(left, UI_CONTROL_TOGGLE, "Missed swings cost a quarter heart",
        "Stops hunters from swinging at everything. Never takes the last quarter heart.", F_PENALTY);
    add_rule(left, UI_CONTROL_TOGGLE, "Props taunt in the last minute",
        "Every hidden prop makes a noise every 20 seconds near the end, so rounds don't stall.", F_TAUNT);
    add_rule(left, UI_CONTROL_TOGGLE, "Start the next round automatically",
        "After the scoreboard, a new round starts with new hunters.", F_NEXT);
    add_rule(left, UI_CONTROL_TOGGLE, "List this room publicly",
        "Anyone can find and join it from the public list.", F_PUBLIC);

    UiControlDesc reset = UI_CONTROL_DESC_INIT;
    reset.kind = UI_CONTROL_BUTTON;
    reset.label = "Reset scores";
    reset.on_pressed = [](ModContext*, void*) { match::reset_scores(); };
    reset.is_disabled = [](ModContext*, void*) { return !net::is_host(); };
    svc_ui->pane_add_control(mod_ctx, left, &reset, nullptr);
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
                std::string map = r.map == 255 ? "Random maps" : map_info(r.map).name;
                labels.push_back(r.code + "   " + r.label + "   " + std::to_string(r.players) + "/" +
                                 std::to_string(r.max) + "   " + kModes[r.mode == 1 ? 1 : 0] + ", " + map);
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
    const bool props = match::get().settings.mode == Mode::PropHunt;
    switch (r) {
    case Role::Hider: return props ? "Prop" : "Hider";
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
        std::string label = std::string(net::member(id).name) + "   " + color_of(p.color).name + "   " +
                            role_name(p.role) + (p.found ? " (found)" : "") + "   " + std::to_string(p.score) +
                            " pts";
        if (id == net::host_id()) label += "   [host]";
        if (id == net::self_id()) label += "   (you)";
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

ModResult build_play(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle, void*, ModError*) {
    s_play = PlayTab{};
    svc_ui->pane_add_text(mod_ctx, left, "", &s_play.status);

    // Offline: name, colour, host, join, public games. Buttons that don't apply are disabled.
    UiControlDesc name = UI_CONTROL_DESC_INIT;
    name.kind = UI_CONTROL_STRING;
    name.label = "Your name";
    name.binding = UI_BINDING_CONFIG_VAR;
    name.config_var = settings::name_var();
    name.max_length = 20;
    svc_ui->pane_add_control(mod_ctx, left, &name, nullptr);

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
    svc_ui->pane_add_control(mod_ctx, left, &color, nullptr);

    const auto offline = [](ModContext*, void*) { return net::status() != net::Status::Offline; };
    button(left, "Host a game", [](ModContext*, void*) {
        s_error.clear();
        net::host_room(settings::server(), settings::name());
    }, offline, "Creates a room and copies its code to your clipboard. Send it to your friends.");

    UiControlDesc code = UI_CONTROL_DESC_INIT;
    code.kind = UI_CONTROL_STRING;
    code.label = "Room code";
    code.binding = UI_BINDING_CONFIG_VAR;
    code.config_var = settings::room_code_var();
    code.max_length = 8;
    svc_ui->pane_add_control(mod_ctx, left, &code, nullptr);
    button(left, "Join room", [](ModContext*, void*) { join_code(settings::room_code()); }, offline);
    button(left, "Paste code and join", [](ModContext*, void*) {
        char buf[64] = {};
        if (svc_ui->get_clipboard_text(mod_ctx, buf, sizeof(buf), nullptr) == MOD_OK) join_code(buf);
    }, offline);

    svc_ui->pane_add_section(mod_ctx, left, "Public games");
    svc_ui->pane_add_text(mod_ctx, left, "Loading...", &s_play.roomsStatus);
    button(left, "Refresh", [](ModContext*, void*) { refresh_rooms(); }, offline);
    UiListDesc rooms = UI_LIST_DESC_INIT;
    rooms.on_pressed = [](ModContext*, UiListHandle, uint64_t key, void*) {
        if (key < s_rooms.size() && net::status() == net::Status::Offline) join_code(s_rooms[key].code);
    };
    svc_ui->pane_add_list(mod_ctx, left, &rooms, &s_play.rooms);

    // Online: code, players, start.
    svc_ui->pane_add_section(mod_ctx, left, "Your room");
    svc_ui->pane_add_text(mod_ctx, left, "", &s_play.code);
    const auto notOnline = [](ModContext*, void*) { return net::status() != net::Status::Online; };
    button(left, "Copy room code", [](ModContext*, void*) {
        svc_ui->set_clipboard_text(mod_ctx, net::room_code().c_str());
        toast("Hide & Seek", "Room code copied.");
    }, notOnline);
    button(left, "Start round", [](ModContext*, void*) {
        std::string why;
        if (!match::can_start(&why)) {
            toast("Hide & Seek", why, "warning");
            return;
        }
        match::start_round();
        if (s_window != 0) svc_ui->window_close(mod_ctx, s_window);
    }, [](ModContext*, void*) { return !net::is_host() || match::in_round(); },
        "Everyone is warped to the map. Hunters wait while the props hide.");
    button(left, "End round now", [](ModContext*, void*) { match::end_round(); },
        [](ModContext*, void*) { return !net::is_host() || !match::in_round(); });
    button(left, "Leave room", [](ModContext*, void*) {
        net::leave_room();
        match::on_disconnected();
    }, [](ModContext*, void*) { return net::status() == net::Status::Offline; });
    UiListDesc players = UI_LIST_DESC_INIT;
    players.on_pressed = [](ModContext*, UiListHandle, uint64_t, void*) {};
    svc_ui->pane_add_list(mod_ctx, left, &players, &s_play.players);

    refresh_rooms();
    return MOD_OK;
}

ModResult update_play(ModContext*, void*, ModError*) {
    std::string status;
    switch (net::status()) {
    case net::Status::Offline:
        status = s_error.empty() ? "Host a game, or join one with a room code." : s_error;
        break;
    case net::Status::Connecting: status = "Connecting..."; break;
    case net::Status::Online:
        status = net::is_host() ? "You're the host. Pick the rules in the Rules tab, then start a round."
                                : "You're in! The host starts the rounds.";
        break;
    }
    if (s_play.status != 0) svc_ui->elem_set_text(mod_ctx, s_play.status, status.c_str());
    if (s_play.code != 0) {
        const std::string code = net::status() == net::Status::Online ? "Room code: " + net::room_code() : "Not in a room";
        svc_ui->elem_set_text(mod_ctx, s_play.code, code.c_str());
    }
    update_players();
    return MOD_OK;
}

// ---- help and settings -----------------------------------------------------------------------

ModResult build_help(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, left, "Prop Hunt");
    svc_ui->pane_add_rml(mod_ctx, left,
        "<p><b>Props</b> turn into an object and hide in plain sight. <b>Hunters</b> wait with a black "
        "screen, then have until the timer runs out to hit every prop with their sword.</p>"
        "<p>Props: <b>D-pad right</b> copies the pot, crate or barrel you stand next to (or picks the "
        "next prop), <b>D-pad left</b> goes back, <b>D-pad down</b> taunts for a bonus point.</p>"
        "<p>Hunters: swing with <b>B</b>. A swing that hits no prop costs a quarter heart.</p>",
        nullptr);
    svc_ui->pane_add_section(mod_ctx, left, "Hide & Seek");
    svc_ui->pane_add_rml(mod_ctx, left,
        "<p>Everyone stays Link. Hunters catch hiders by touching them or hitting them.</p>", nullptr);
    svc_ui->pane_add_section(mod_ctx, left, "Points");
    svc_ui->pane_add_rml(mod_ctx, left,
        "<p>Props: 1 point per 10 seconds hidden, 5 for surviving the round, 1 per taunt (once every 10 "
        "seconds). Hunters: 5 per find.</p>",
        nullptr);
    svc_ui->pane_add_section(mod_ctx, left, "Tips");
    svc_ui->pane_add_rml(mod_ctx, left,
        "<p>Stand still next to real pots. Moving props give themselves away. Doors and loading zones "
        "work: you get a few seconds of safety after entering a new area.</p>"
        "<p>For the same world for everyone (no twilight, same items), start from the "
        "<b>Hide &amp; Seek</b> game mode on the title screen.</p>",
        nullptr);
    return MOD_OK;
}

ModResult build_settings(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle, void*, ModError*) {
    UiControlDesc tags = UI_CONTROL_DESC_INIT;
    tags.kind = UI_CONTROL_TOGGLE;
    tags.label = "Show name tags";
    tags.binding = UI_BINDING_CONFIG_VAR;
    tags.config_var = settings::name_tags_var();
    svc_ui->pane_add_control(mod_ctx, left, &tags, nullptr);

    svc_ui->pane_add_section(mod_ctx, left, "Server");
    UiControlDesc server = UI_CONTROL_DESC_INIT;
    server.kind = UI_CONTROL_STRING;
    server.label = "Server address";
    server.help_rml = "Leave this alone unless you run your own server (see server/README.md). "
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
    tabs[2].title = "How to play";
    tabs[2].build = build_help;
    tabs[3].title = "Settings";
    tabs[3].build = build_settings;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 4;
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
        "Online Prop Hunt and Hide & Seek for 2-16 players. Open it from the Hide & Seek tab in the menu bar.",
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
