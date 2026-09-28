#pragma once

// Connection to the relay (server/). One room at a time.

#include "common.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hs::net {

enum class Status { Offline, Connecting, Online };

constexpr uint8_t kToEveryone = 0;
constexpr uint8_t kToHost = 255;

struct Member {
    bool present = false;
    char name[24] = {};
};

struct Events {
    std::function<void()> welcome;                 // we're in; members and ids are known
    std::function<void(uint8_t id)> joined;        // someone else joined
    std::function<void(uint8_t id)> left;          // someone left (still readable in member())
    std::function<void(uint8_t id)> hostChanged;
    std::function<void(uint8_t from, const uint8_t* data, size_t size)> message;
    std::function<void(const std::string& why)> disconnected;
};

void set_events(Events events);

// Starts a new room / joins one. The result arrives as welcome() or disconnected().
void host_room(const std::string& server, const std::string& name);
void join_room(const std::string& server, const std::string& code, const std::string& name);
void leave_room();

// Drains the WebSocket queue. Call once per frame.
void update();

Status status();
const std::string& room_code();
uint8_t self_id();
uint8_t host_id();
bool is_host();
const Member& member(int id);
int member_count();

// Game payload to one player, kToHost, or kToEveryone (everyone but us).
void send(uint8_t to, const std::vector<uint8_t>& payload);
// Host only: public listing ({op:"meta"}) and kicking.
void send_meta(bool isPublic, const std::string& label, int mode, int map, int phase);
void kick(uint8_t id);

struct PublicRoom {
    std::string code;
    std::string label;
    int players = 0;
    int max = 0;
    int mode = 0;
    int map = 0;
    int phase = 0;
};
// GET <server>/rooms. The callback runs on the game thread.
void fetch_rooms(const std::string& server, std::function<void(bool ok, std::vector<PublicRoom>)> done);

// "wss://x/" -> "https://x", for plain HTTP requests to the relay.
std::string http_base(const std::string& server);

}  // namespace hs::net
