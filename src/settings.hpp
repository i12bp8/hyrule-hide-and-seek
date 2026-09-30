#pragma once

// Persistent per-player settings (Dusklight config.json).

#include "match.hpp"

#include <mods/svc/config.h>

#include <string>

namespace hs::settings {

void init();

std::string name();
uint8_t color();
void set_color(uint8_t color);
std::string server();
std::string room_code();
void set_room_code(const std::string& code);
bool name_tags();
bool control_hints();

// The host's last-used rules, so a new room starts with them.
match::Settings host_rules();
void save_host_rules(const match::Settings& s);

// For UI controls bound straight to config vars.
ConfigVarHandle name_var();
ConfigVarHandle server_var();
ConfigVarHandle room_code_var();
ConfigVarHandle name_tags_var();
ConfigVarHandle control_hints_var();

}  // namespace hs::settings
