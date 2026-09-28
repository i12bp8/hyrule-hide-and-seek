// A stand-in for src/net.cpp: records what the rules send instead of talking to a relay.
#include "fake_net.hpp"

namespace hs::net {

FakeNet g_fake;

void set_events(Events) {}
void host_room(const std::string&, const std::string&) {}
void join_room(const std::string&, const std::string&, const std::string&) {}
void leave_room() {}
void update() {}
Status status() { return g_fake.online ? Status::Online : Status::Offline; }
const std::string& room_code() { return g_fake.code; }
uint8_t self_id() { return g_fake.self; }
uint8_t host_id() { return g_fake.host; }
bool is_host() { return g_fake.online && g_fake.self == g_fake.host; }
const Member& member(int id) {
    static const Member none;
    return id >= 1 && id <= kMaxPlayers ? g_fake.members[id] : none;
}
int member_count() {
    int n = 0;
    for (int id = 1; id <= kMaxPlayers; ++id) n += g_fake.members[id].present ? 1 : 0;
    return n;
}
void send(uint8_t to, const std::vector<uint8_t>& payload) { g_fake.sent.push_back({to, payload}); }
void send_meta(bool, const std::string&, int, int, int) { ++g_fake.metaCount; }
void kick(uint8_t) {}
void fetch_rooms(const std::string&, std::function<void(bool, std::vector<PublicRoom>)> done) { done(false, {}); }
std::string http_base(const std::string& s) { return s; }

}  // namespace hs::net
