#pragma once

// The Hide & Seek window (menu bar tab): host, join, public games, rules, help, settings.

#include <string>

namespace hs::ui {

bool init();
void open();
bool is_open();
void shutdown();

void on_welcome();
void on_disconnected(const std::string& why);

}  // namespace hs::ui
