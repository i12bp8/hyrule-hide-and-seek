#include "net.hpp"

#include "json.hpp"
#include "protocol.hpp"

#include <mods/svc/http.hpp>
#include <mods/svc/websocket.hpp>

#include <cctype>

namespace hs::net {

namespace {

Events s_events;
mods::ws::Connection s_conn;
enum class Transport { None, WebSocket, Http };
Transport s_transport = Transport::None;
Status s_status = Status::Offline;
std::string s_code;
uint8_t s_self = 0;
uint8_t s_host = 0;
Member s_members[kSlots];
mods::http::Pending s_roomsRequest;
mods::http::Pending s_httpOpen;
mods::http::Pending s_httpPoll;
mods::http::Pending s_httpSend;
std::string s_httpBase;
std::string s_httpSession;
std::string s_httpOut;
uint32_t s_httpGeneration = 0;
int s_httpFailures = 0;
uint64_t s_httpRetryAt = 0;

constexpr size_t kHttpMessageMax = 4096;
constexpr size_t kHttpBatchMax = 64 * 1024;

std::string url_encode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string trim_slash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

void reset_members() {
    for (Member& m : s_members) m = Member{};
}

void set_member(int id, const std::string& name) {
    if (id < 1 || id > kMaxPlayers) return;
    s_members[id].present = true;
    copy_str(s_members[id].name, name.c_str());
}

std::string explain(const std::string& why, const std::string& detail) {
    if (why == "no_room") return "No room with that code. Check the code, or the host left.";
    if (why == "full") return "That room is full (16 players).";
    if (why == "version")
        return "That room runs a different version of Hyrule Hide & Seek. Everyone needs the same "
               "version.";
    if (why == "kicked") return "The host removed you from the room.";
    if (why == "taken") return "Could not create a room, try again.";
    return detail.empty() ? why : why + " (" + detail + ")";
}

void closed(const std::string& why);
void control(std::string_view text);

void fire_http_leave(const std::string& base, const std::string& session) {
    if (base.empty() || session.empty()) return;
    mods::http::Request request{
        .method = HTTP_METHOD_POST,
        .url = base + "/session/" + session + "/leave",
        .totalTimeoutMs = 3000,
        .maxBodyBytes = 1024,
    };
    auto pending = mods::http::request(request, [](mods::http::Response) {});
    pending.detach();
}

void drop_transport(bool tellServer) {
    const std::string base = s_httpBase;
    const std::string session = s_httpSession;
    if (s_transport == Transport::WebSocket) {
        if (tellServer && s_conn) {
            (void)s_conn.close(1000, "left");
        } else {
            s_conn.detach();
        }
    }
    s_conn = mods::ws::Connection{};
    ++s_httpGeneration;
    s_httpOpen = mods::http::Pending{};
    s_httpPoll = mods::http::Pending{};
    s_httpSend = mods::http::Pending{};
    s_httpBase.clear();
    s_httpSession.clear();
    s_httpOut.clear();
    s_httpFailures = 0;
    s_httpRetryAt = 0;
    s_transport = Transport::None;
    if (tellServer) fire_http_leave(base, session);
}

void process_binary(const uint8_t* bytes, size_t size) {
    if (size >= 2 && s_status == Status::Online && s_events.message) {
        s_events.message(bytes[0], bytes + 1, size - 1);
    }
}

bool process_http_batch(const std::vector<uint8_t>& bytes) {
    size_t at = 0;
    while (at < bytes.size()) {
        if (at + 3 > bytes.size()) return false;
        const uint8_t kind = bytes[at++];
        const size_t size = static_cast<size_t>(bytes[at]) |
                            (static_cast<size_t>(bytes[at + 1]) << 8);
        at += 2;
        if (kind > 1 || size > kHttpMessageMax || at + size > bytes.size()) return false;
        if (kind == 0) {
            control({reinterpret_cast<const char*>(bytes.data() + at), size});
        } else {
            process_binary(bytes.data() + at, size);
        }
        at += size;
        if (s_transport != Transport::Http) return true;
    }
    return true;
}

void queue_http(uint8_t kind, const void* data, size_t size) {
    if (size > kHttpMessageMax || s_httpOut.size() + size + 3 > kHttpBatchMax) {
        closed("The connection could not keep up with the game.");
        return;
    }
    s_httpOut.push_back(static_cast<char>(kind));
    s_httpOut.push_back(static_cast<char>(size & 0xff));
    s_httpOut.push_back(static_cast<char>((size >> 8) & 0xff));
    s_httpOut.append(static_cast<const char*>(data), size);
}

void start_http_poll() {
    if (s_httpSession.empty() || s_httpPoll || now_ms() < s_httpRetryAt) return;
    const uint32_t generation = s_httpGeneration;
    mods::http::Request request{
        .url = s_httpBase + "/session/" + s_httpSession + "/poll",
        .idleTimeoutMs = 12000,
        .totalTimeoutMs = 15000,
        .maxBodyBytes = 128 * 1024,
    };
    s_httpPoll = mods::http::request(request, [generation](mods::http::Response response) {
        if (generation != s_httpGeneration || s_transport != Transport::Http) return;
        if (!response.ok()) {
            ++s_httpFailures;
            s_httpRetryAt = now_ms() + 400;
            if (response.statusCode == 404 || response.statusCode == 410 || s_httpFailures >= 3) {
                closed("Disconnected from the Hide & Seek server.");
            }
            return;
        }
        s_httpFailures = 0;
        if (!process_http_batch(response.body)) {
            closed("The Hide & Seek server sent an invalid message.");
        }
    });
    if (!s_httpPoll) closed("Could not poll the Hide & Seek server.");
}

void start_http_send() {
    if (s_httpSession.empty() || s_httpSend || s_httpOut.empty()) return;
    const uint32_t generation = s_httpGeneration;
    mods::http::Request request{
        .method = HTTP_METHOD_POST,
        .url = s_httpBase + "/session/" + s_httpSession + "/send",
        .headers = {{"content-type", "application/octet-stream"}},
        .body = std::move(s_httpOut),
        .totalTimeoutMs = 8000,
        .maxBodyBytes = 1024,
    };
    s_httpOut.clear();
    s_httpSend = mods::http::request(request, [generation](mods::http::Response response) {
        if (generation != s_httpGeneration || s_transport != Transport::Http) return;
        if (!response.ok()) closed("Could not send data to the Hide & Seek server.");
    });
    if (!s_httpSend) closed("Could not send data to the Hide & Seek server.");
}

void start_http(const std::string& server, const std::string& path) {
    s_transport = Transport::Http;
    s_httpBase = http_base(server);
    // Stock HttpService accepts HTTPS only, even for loopback. WebSocket-capable builds can
    // still use a plain local relay, but this fallback must fail with an actionable message.
    if (s_httpBase.rfind("https://", 0) != 0) {
        closed("This Dusklight build requires an HTTPS relay. Use the default server or a secure custom relay.");
        return;
    }
    const uint32_t generation = s_httpGeneration;
    mods::http::Request request{
        .method = HTTP_METHOD_POST,
        .url = s_httpBase + "/session" + path,
        .totalTimeoutMs = 10000,
        .maxBodyBytes = 16 * 1024,
    };
    s_httpOpen = mods::http::request(request, [generation](mods::http::Response response) {
        if (generation != s_httpGeneration || s_transport != Transport::Http) return;
        json::Value body;
        if (!response.ok() ||
            !json::parse({reinterpret_cast<const char*>(response.body.data()), response.body.size()}, body) ||
            body["session"].str().empty())
        {
            closed("Could not reach the Hide & Seek server. Is the server address right?");
            return;
        }
        s_httpSession = body["session"].str();
        mods::log::info("Dusklight WebSockets unavailable; using the HTTP relay fallback");
    });
    if (!s_httpOpen) {
        mods::log::warn("HTTP relay open failed (service result {})", static_cast<int>(s_httpOpen.result()));
        closed("Dusklight's network services are unavailable.");
    }
}

void connect_room(const std::string& server, const std::string& path) {
    leave_room();
    reset_members();
    s_status = Status::Connecting;
    mods::ws::Options options;
    options.url = trim_slash(server) + path;
    options.connectTimeoutMs = 10000;
    options.keepaliveIntervalMs = 15000;
    options.maxMessageBytes = 64 * 1024;
    s_conn = mods::ws::connect(options);
    if (s_conn) {
        s_transport = Transport::WebSocket;
        return;
    }
    if (s_conn.result() == MOD_UNAVAILABLE) {
        s_conn = mods::ws::Connection{};
        start_http(server, path);
        return;
    }
    closed("Could not start a connection to the server.");
}

void closed(const std::string& why) {
    const bool wasOn = s_status != Status::Offline;
    drop_transport(false);
    s_status = Status::Offline;
    s_code.clear();
    s_self = 0;
    s_host = 0;
    if (wasOn && s_events.disconnected) s_events.disconnected(why);
}

void control(std::string_view text) {
    json::Value msg;
    if (!json::parse(text, msg) || !msg.is_object()) return;
    const std::string& op = msg["op"].str();
    if (op == "welcome") {
        s_code = msg["code"].str();
        s_self = static_cast<uint8_t>(msg["you"].num());
        s_host = static_cast<uint8_t>(msg["host"].num());
        reset_members();
        for (const json::Value& p : msg["players"].array) set_member(p["id"].num(), p["name"].str());
        s_status = Status::Online;
        mods::log::info("joined room {} as player {} (host {})", s_code, s_self, s_host);
        if (s_events.welcome) s_events.welcome();
    } else if (op == "join") {
        const int id = msg["id"].num();
        set_member(id, msg["name"].str());
        if (s_events.joined) s_events.joined(static_cast<uint8_t>(id));
    } else if (op == "leave") {
        const int id = msg["id"].num();
        if (id >= 1 && id <= kMaxPlayers) {
            if (s_events.left) s_events.left(static_cast<uint8_t>(id));
            s_members[id].present = false;
        }
    } else if (op == "host") {
        s_host = static_cast<uint8_t>(msg["id"].num());
        if (s_events.hostChanged) s_events.hostChanged(s_host);
    } else if (op == "error") {
        const std::string why = explain(msg["why"].str(), msg["detail"].str());
        mods::log::warn("relay refused: {}", why);
        if (s_transport == Transport::WebSocket) s_conn.close(1000, "refused");
        closed(why);
    }
}

}  // namespace

void set_events(Events events) {
    s_events = std::move(events);
}

void host_room(const std::string& server, const std::string& name) {
    connect_room(server,
        "/host?name=" + url_encode(name) + "&v=" + std::to_string(kProtocolVersion));
}

void join_room(const std::string& server, const std::string& code, const std::string& name) {
    std::string clean;
    for (const char c : code) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            clean += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    connect_room(server, "/join/" + clean + "?name=" + url_encode(name) +
                             "&v=" + std::to_string(kProtocolVersion));
}

void leave_room() {
    drop_transport(true);
    s_status = Status::Offline;
    s_code.clear();
    s_self = 0;
    s_host = 0;
}

void update() {
    if (s_transport == Transport::Http) {
        start_http_poll();
        start_http_send();
        return;
    }
    if (s_transport != Transport::WebSocket) return;
    mods::ws::Event event;
    while (mods::ws::poll(event)) {
        if (event.handle != s_conn.handle()) continue;  // a connection we already dropped
        switch (event.type) {
        case WEBSOCKET_EVENT_MESSAGE:
            if (event.messageKind == WEBSOCKET_MESSAGE_TEXT) {
                control({reinterpret_cast<const char*>(event.data.data()), event.data.size()});
            } else {
                process_binary(reinterpret_cast<const uint8_t*>(event.data.data()), event.data.size());
            }
            break;
        case WEBSOCKET_EVENT_CLOSED: {
            std::string why = "Disconnected from the server.";
            if (event.error == WEBSOCKET_ERROR_TIMEOUT) {
                why = "The server did not answer. Check your internet connection.";
            } else if (event.error == WEBSOCKET_ERROR_HANDSHAKE ||
                       event.error == WEBSOCKET_ERROR_INVALID_URL ||
                       event.error == WEBSOCKET_ERROR_UNSUPPORTED_SCHEME) {
                why = "Could not reach the Hide & Seek server. Is the server address right?";
            }
            if (!event.message.empty()) mods::log::warn("connection closed: {}", event.message);
            closed(why);
            break;
        }
        default: break;
        }
    }
}

Status status() {
    return s_status;
}

const std::string& room_code() {
    return s_code;
}

uint8_t self_id() {
    return s_self;
}

uint8_t host_id() {
    return s_host;
}

bool is_host() {
    return s_status == Status::Online && s_self != 0 && s_self == s_host;
}

const Member& member(int id) {
    static const Member none;
    return id >= 1 && id <= kMaxPlayers ? s_members[id] : none;
}

int member_count() {
    int n = 0;
    for (int id = 1; id <= kMaxPlayers; ++id) n += s_members[id].present ? 1 : 0;
    return n;
}

void send(uint8_t to, const std::vector<uint8_t>& payload) {
    if (s_status != Status::Online || payload.empty()) return;
    std::vector<std::byte> frame(payload.size() + 1);
    frame[0] = static_cast<std::byte>(to);
    std::memcpy(frame.data() + 1, payload.data(), payload.size());
    if (s_transport == Transport::Http) {
        queue_http(1, frame.data(), frame.size());
    } else {
        // MOD_CONFLICT means the outbound queue is full; position updates can be dropped.
        (void)s_conn.send_binary(frame);
    }
}

void send_meta(bool isPublic, const std::string& label, int mode, int map, int phase) {
    if (!is_host()) return;
    std::string text = "{\"op\":\"meta\",\"public\":";
    text += isPublic ? "true" : "false";
    text += ",\"label\":\"" + json::escape(label) + "\"";
    text += ",\"mode\":" + std::to_string(mode);
    text += ",\"map\":" + std::to_string(map);
    text += ",\"phase\":" + std::to_string(phase) + "}";
    if (s_transport == Transport::Http) {
        queue_http(0, text.data(), text.size());
    } else {
        (void)s_conn.send_text(text);
    }
}

void kick(uint8_t id) {
    if (!is_host()) return;
    const std::string text = "{\"op\":\"kick\",\"id\":" + std::to_string(id) + "}";
    if (s_transport == Transport::Http) {
        queue_http(0, text.data(), text.size());
    } else {
        (void)s_conn.send_text(text);
    }
}

std::string http_base(const std::string& server) {
    std::string url = trim_slash(server);
    if (url.rfind("wss://", 0) == 0) return "https://" + url.substr(6);
    if (url.rfind("ws://", 0) == 0) return "http://" + url.substr(5);
    return url;
}

void fetch_rooms(const std::string& server, std::function<void(bool, std::vector<PublicRoom>)> done) {
    mods::http::Request request{
        .url = http_base(server) + "/rooms?v=" + std::to_string(kProtocolVersion),
        .totalTimeoutMs = 8000,
        .maxBodyBytes = 64 * 1024,
    };
    s_roomsRequest = mods::http::request(request, [done](mods::http::Response response) {
        std::vector<PublicRoom> rooms;
        json::Value body;
        if (!response.ok() ||
            !json::parse({reinterpret_cast<const char*>(response.body.data()), response.body.size()}, body)) {
            done(false, rooms);
            return;
        }
        for (const json::Value& r : body["rooms"].array) {
            rooms.push_back({r["code"].str(), r["label"].str(), r["players"].num(), r["max"].num(),
                r["mode"].num(), r["map"].num(), r["phase"].num()});
        }
        done(true, rooms);
    });
    if (!s_roomsRequest) done(false, {});
}

}  // namespace hs::net
