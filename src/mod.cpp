// Hyrule Hide & Seek: online Prop Hunt and Hide & Seek for Dusklight.
//
//   net       relay connection (server/)            match     the rules, run by the room's host
//   local     the local Link: state, warp, controls puppet    other players and props in the world
//   linkkit   Link's models per tunic colour, anims  hud       timer, banners, name tags
//   ui        the Hide & Seek window                 game_mode the title screen mode and its save

#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/config.h"
#include "mods/svc/game_mode.h"
#include "mods/svc/hook.h"
#include "mods/svc/http.h"
#include "mods/svc/log.hpp"
#include "mods/svc/texture.h"
#include "mods/svc/ui.h"
#include "mods/svc/websocket.h"

#include "game_mode.hpp"
#include "linkkit.hpp"
#include "local.hpp"
#include "match.hpp"
#include "net.hpp"
#include "puppet.hpp"
#include "settings.hpp"
#include "ui.hpp"

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
// Dusklight 2.0.2's official Linux build has no WebSocket backend. net.cpp falls back to the
// HttpService there, so the WebSocket import must not prevent the mod from loading.
IMPORT_OPTIONAL_SERVICE(WebSocketService, svc_websocket);
IMPORT_SERVICE(HttpService, svc_http);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(ActorService, svc_actor);
IMPORT_OPTIONAL_SERVICE(TextureService, svc_texture);
IMPORT_OPTIONAL_SERVICE(GameModeService, svc_game_mode);

namespace {

void wire_network() {
    hs::net::Events events;
    events.welcome = [] {
        hs::match::on_welcome();
        if (hs::net::is_host()) hs::match::set_settings(hs::settings::host_rules());
        hs::match::set_wanted_color(hs::settings::color());
        hs::ui::on_welcome();
    };
    events.joined = hs::match::on_joined;
    events.left = hs::match::on_left;
    events.hostChanged = hs::match::on_host_changed;
    events.message = hs::match::on_message;
    events.disconnected = [](const std::string& why) {
        hs::match::on_disconnected();
        hs::ui::on_disconnected(why);
    };
    hs::net::set_events(std::move(events));
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    hs::settings::init();
    wire_network();
    hs::local::init();
    if (!hs::puppet::register_actor()) {
        return mods::set_error(error, MOD_ERROR, "could not register the player actor");
    }
    hs::ui::init();
    hs::game_mode::init();
    mods::log::info("Hyrule Hide & Seek {} ready (protocol {}, server {})", HS_MOD_VERSION,
        hs::kProtocolVersion, hs::settings::server());
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    hs::net::update();
    hs::match::update();
    hs::local::update();
    hs::puppet::update();
    hs::game_mode::update();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    hs::net::leave_room();
    hs::local::shutdown();
    hs::ui::shutdown();
    // The puppets use memory from linkkit's heap, so they must be gone before it is freed.
    if (hs::puppet::unregister_actor()) {
        hs::linkkit::shutdown();
    } else {
        mods::log::warn("puppets still alive at shutdown; keeping their memory");
    }
    return MOD_OK;
}

}
